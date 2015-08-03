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
long RECORDS;
MDL::EncVector sum_ctxts(const std::vector<MDL::EncVector>& ctxts)
{
    std::vector<std::thread>    workers;
    std::vector<MDL::EncVector> partials(WORKER_NR, ctxts[0].getPubKey());
    std::atomic<size_t> counter(WORKER_NR);

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

    return partials[0];
}

MDL::EncVector load_file(const EncryptedArray& ea,
						 const FHEPubKey     & pk,
						 const MDL::Matrix<long> &data,
						 const long domain,
						 MDL::Timer &encTimer,
						 MDL::Timer &evalTimer)
{
	std::vector<MDL::EncVector> ctxts(data.rows(), pk);
	std::atomic<size_t> counter(0);
	std::vector<std::thread> workers;

	encTimer.start();
	for (long wr = 0; wr < WORKER_NR; wr++) {
		workers.push_back(std::move(std::thread([&counter, &ea, &domain,
												&ctxts, &data]() {
			size_t next;

			while ((next = counter.fetch_add(1)) < data.rows()) {
				auto indicator = MDL::encoding::staircase(data[next][0], ea, domain);
				ctxts[next].pack(indicator, ea);
			} })));
	}

	for (auto && wr : workers) wr.join();
	encTimer.end();

	evalTimer.start();
	auto sum = sum_ctxts(ctxts);
	evalTimer.end();
	return sum;
}

std::vector<MDL::GTResult>k_percentile(const MDL::EncVector& ctxt,
									   const FHEPubKey     & pk,
									   const EncryptedArray& ea,
									   long                  records_nr,
									   long                  domain,
									   long                  k)
{
	MDL::EncVector oth(pk);
	long kpercentile =  k * records_nr / 100;
	MDL::Vector<long> percentile(ea.size(), kpercentile);
	long plainSpace  = ea.getContext().alMod.getPPowR();
	std::vector<MDL::GTResult> gtresults(domain);
	std::atomic<size_t> counter(0);
	std::vector<std::thread> workers;

	oth.pack(percentile, ea);

	for (long wr = 0; wr < WORKER_NR; wr++) {
		workers.push_back(std::thread([&ctxt, &ea, &counter, &oth, &records_nr,
									  &domain, &plainSpace, &gtresults]() {
									  size_t d;

									  while ((d = counter.fetch_add(1)) < domain) {
									  auto tmp(ctxt);
									  replicate(ea, tmp, d);
									  MDL::GTInput input = { oth, tmp, records_nr, plainSpace };
									  gtresults[d] = MDL::GT(input, ea);
									  }
									  }));
	}

	for (auto& wr : workers) wr.join();

	return gtresults;
}

long decrypt(const std::vector<MDL::GTResult>& gtresults,
			 const FHESecKey& sk,
			 const EncryptedArray& ea, long kpercent)
{
	std::vector<bool>   results(gtresults.size());
	std::atomic<size_t> counter(0);
	std::vector<std::thread> workers;

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

	bool prev = true;

	for (size_t i = 0; i < results.size(); i++) {
		if (prev && !results[i]) {
			return i;
		}
	}
	return -1;
}

int main(int argc, char *argv[]) {
	long m, p, r, L;
	long TRIAL = 10;
	ArgMapping argmap;
	MDL::Timer total, enc, eval, dec;
	argmap.arg("m", m, "m");
	argmap.arg("L", L, "L");
	argmap.arg("p", p, "p");
	argmap.arg("r", r, "r");
	argmap.arg("B", RECORDS, "RECORDS");
	argmap.arg("T", TRIAL, "TRIAL");
	argmap.parse(argc, argv);
	printf("nr enc eval dec total\n");
	auto data = load_csv("adult.data", RECORDS);
	for (long trial = 0; trial < TRIAL; trial++) {
		total.start();
		FHEcontext context(m, p, r);
		buildModChain(context, L);
		FHESecKey sk(context);
		sk.GenSecKey(64);
		addSome1DMatrices(sk);
		FHEPubKey pk = sk;
		auto G = context.alMod.getFactorsOverZZ()[0];
		EncryptedArray ea(context, G);
		auto ctxt = load_file(ea, pk, data, 100, enc, eval);
		long kpercent  = 50;
		eval.start();
		auto gtresults = k_percentile(ctxt,
									  pk, ea,
									  data.rows(),
									  100,
									  kpercent);
		eval.end();
		dec.start();
		decrypt(gtresults, sk, ea, kpercent);
		dec.end();
		total.end();
	}

	printf("%ld %f %f %f %f\n", data.rows(), enc.second() / TRIAL,
		   eval.second() / TRIAL,
		   dec.second() / TRIAL,
		   total.second() / TRIAL);
	return 0;
}
