#include <fhe/FHEContext.h>
#include <fhe/FHE.h>
#include <fhe/NumbTh.h>
#include <fhe/EncryptedArray.h>
#include <fhe/replicate.h>

#include <utils/FileUtils.hpp>
#include <utils/timer.hpp>
#include <utils/encoding.hpp>

#include <protocol/Gt.hpp>

#include <thread>
#include <atomic>
#ifdef FHE_THREADS
long WORKER_NR = 8;
#else // ifdef FHE_THREADS
long WORKER_NR = 1;
#endif // ifdef FHE_THREADS

MDL::EncVector sum_ctxts(const std::vector<MDL::EncVector>& ctxts)
{
    std::vector<std::thread>    workers;
    std::vector<MDL::EncVector> partials(WORKER_NR, ctxts[0].getPubKey());
    std::atomic<size_t> counter(WORKER_NR);
    MDL::Timer timer;

    timer.start();

    for (long i = 0; i < WORKER_NR; i++) {
        partials[i] = ctxts[i];
        workers.push_back(std::move(std::thread([&counter, &ctxts]
                                                    (MDL::EncVector& ct) {
            size_t next;

            while ((next = counter.fetch_add(1)) < ctxts.size()) {
                ct += ctxts[next];
            }
        }, std::ref(partials[i]))));
    }

    for (auto && wr : workers) wr.join();

    for (long i = 1; i < WORKER_NR; i++) partials[0] += partials[i];
    timer.end();

    printf("Sum %zd ctxts with %ld workers costed %f sec\n", ctxts.size(),
           WORKER_NR, timer.second());

    return partials[0];
}

std::pair<MDL::EncVector, long>load_file(const EncryptedArray& ea,
                                         const FHEPubKey     & pk)
{
    auto data = load_csv("adult.data", 2000);
    std::vector<MDL::EncVector> ctxts(data.rows(), pk);
    std::atomic<size_t> counter(0);
    std::vector<std::thread> workers;
    MDL::Timer timer;

    timer.start();

    for (long wr = 0; wr < WORKER_NR; wr++) {
        workers.push_back(std::move(std::thread([&counter, &ea,
                                                 &ctxts, &data]() {
            size_t next;

            while ((next = counter.fetch_add(1)) < data.rows()) {
                auto indicator = MDL::encoding::staircase(data[next][0], ea);
                ctxts[next].pack(indicator, ea);
            }
        })));
    }
    timer.end();

    for (auto && wr : workers) wr.join();
    printf("Encrypt %ld records with %ld workers costed %f sec.\n",
           data.rows(), WORKER_NR, timer.second());
    return { sum_ctxts(ctxts), data.rows() };
}

std::vector<MDL::GTResult<void>>
k_percentile(const MDL::EncVector& ctxt,
             const FHEPubKey     & pk,
             const EncryptedArray& ea,
             long                  records_nr,
             long                  domain,
             long                  k)
{
    MDL::Timer timer;
    MDL::EncVector oth(pk);
    long kpercentile =  k * records_nr / 100;
    MDL::Vector<long> percentile(ea.size(), kpercentile);
    long plainSpace  = ea.getContext().alMod.getPPowR();
    std::vector<MDL::GTResult<void>> gtresults(domain);
    std::atomic<size_t> counter(0);
    std::vector<std::thread> workers;

    oth.pack(percentile, ea);
    timer.start();

    for (long wr = 0; wr < WORKER_NR; wr++) {
        workers.push_back(std::thread([&ctxt, &ea, &counter, &oth, &records_nr,
                                       &domain, &plainSpace, &gtresults]() {
            size_t d;

            while ((d = counter.fetch_add(1)) < domain) {
                auto tmp(ctxt);
                replicate(ea, tmp, d);
                MDL::GTInput<void> input = { oth, tmp, records_nr, plainSpace };
                gtresults[d] = MDL::GT(input, ea);
            }
        }));
    }

    for (auto& wr : workers) wr.join();
    timer.end();
    printf("call GT on Domain %ld used %ld workers costed %f second\n",
           records_nr, WORKER_NR, timer.second());
    return gtresults;
}

void decrypt(const std::vector<MDL::GTResult<void>>& gtresults,
             const FHESecKey& sk,
             const EncryptedArray& ea, long kpercent)
{
    std::vector<bool>   results(gtresults.size());
    std::atomic<size_t> counter(0);
    std::vector<std::thread> workers;
    MDL::Timer timer;

    timer.start();

    for (int wr = 0; wr < WORKER_NR; wr++) {
        workers.push_back(std::thread([&gtresults, &ea, &sk,
                                       &results, &counter]() {
            size_t next;

            while ((next = counter.fetch_add(1)) < results.size()) {
                results[next] = MDL::decrypt_gt_result(gtresults[next],
                                                       sk,
                                                       ea);
            }
        }));
    }

    for (auto && wr : workers) wr.join();
    timer.end();
    bool prev = true;

    for (size_t i = 0; i < results.size(); i++) {
        if (prev && !results[i]) {
            printf("%ld-percentile is %zd\n", kpercent, i);
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    long m, p, r, L;
    ArgMapping argmap;

    argmap.arg("m", m, "m");
    argmap.arg("L", L, "L");
    argmap.arg("p", p, "p");
    argmap.arg("r", r, "r");
    argmap.parse(argc, argv);

    FHEcontext context(m, p, r);
    buildModChain(context, L);
    FHESecKey sk(context);
    sk.GenSecKey(64);
    addSome1DMatrices(sk);
    FHEPubKey pk = sk;

    auto G = context.alMod.getFactorsOverZZ()[0];
    EncryptedArray ea(context, G);

    auto data      = load_file(ea, pk);
    long kpercent  = 50;
    auto gtresults = k_percentile(data.first,
                                  pk, ea,
                                  data.second,
                                  100,
                                  kpercent);
    decrypt(gtresults, sk, ea, kpercent);
    return 0;
}
