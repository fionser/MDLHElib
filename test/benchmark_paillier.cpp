#include "paillier/Paillier.hpp"
#include "utils/FileUtils.hpp"
#include "algebra/NDSS.h"
#include "utils/timer.hpp"
#include "utils/encoding.hpp"
#include <vector>
#include <thread>
#ifdef FHE_THREADS
const long work_nr = 8;
#else
const long work_nr = 1;
#endif

std::vector<MDL::Paillier::Ctxt> encrypt(const MDL::Matrix<long> &data,
                                         const MDL::Paillier::PubKey &pk) {
    MDL::Timer timer;
    timer.start();
    MDL::Paillier::Ctxt c(pk);
    std::vector<MDL::Paillier::Ctxt> ctxts(data.rows(), c);
#ifdef OMP
	omp_set_num_threads(sysconf( _SC_NPROCESSORS_ONLN ));
#endif
#pragma omp parallel for
    for (long i = 0; i < data.rows(); i++) {
        pk.Pack(ctxts[i], data[i], 64);
    }
    timer.end();
    printf("Enc %zd records cost %f sec\n", data.rows(), timer.second());
    return ctxts;
}

MDL::Paillier::Ctxt mean(const std::vector<MDL::Paillier::Ctxt> &ctxts) {
    using namespace MDL;
    std::vector<Paillier::Ctxt> parts;
	auto parts_nr = std::min<long>(ctxts.size(), work_nr);
    for (long i = 0; i < parts_nr; i++)
        parts.push_back(ctxts[i]);

    std::atomic<size_t> counter(parts.size());
    std::vector<std::thread> workers;
    auto process = [&](long id) {
        while (true) {
            auto next = counter.fetch_add(1);
            if (next >= ctxts.size()) break;
            parts[id] += ctxts[next];
        }
    };

    MDL::Timer timer;
    timer.start();
    for (long i = 0; i < parts_nr; i++)
        workers.push_back(std::thread(process, i));

    for (auto &&w : workers) w.join();
    for (long i = 1; i < parts_nr; i++)
        parts[0] += parts[i];
    timer.end();

	auto sze = ctxts.size();
    printf("Mean of %zd records cost %f sec\n",
		   sze, timer.second());
	return parts[0];
}

//std::vector<MDL::Paillier::Ctxt> encrypt_for_percentile(const MDL::Matrix<long> &data,
//                                                        const MDL::Paillier::PubKey &pk,
//                                                        long bits,
//                                                        long int feature) {
//    using namespace MDL;
//    std::vector<std::thread> workers;
//    std::atomic<size_t> counter(0);
//    auto nr_prime = pk.GetPrimes().size();
//    auto bit_per_prime = pk.bits_per_prime();
//    auto slot_one_cipher = nr_prime / ((bits + bit_per_prime - 1) / bit_per_prime);
//
//    Timer timer;
//    timer.start();
//    for (long wr = 0; wr < work_nr; wr++) {
//        workers.push_back(std::move(std::thread([&]() {
//            size_t next;
//
//            while ((next = counter.fetch_add(1)) < data.rows()) {
//                auto indicator = encoding::staircase(data[next][feature],
//                                                     slot_one_cipher);
//            }
//        })));
//    }
//
//    timer.end();
//}

int main(int argc, char *argv[]) {
    std::string file;
    long key_len = 1024;
    ArgMapping argmap;
    argmap.arg("f", file, "file");
    argmap.arg("k", key_len, "key length");
    argmap.parse(argc, argv);

    auto data = load_csv(file);

    auto keys = MDL::Paillier::GenKey(key_len);
    MDL::Paillier::SecKey sk(keys.first);
    MDL::Paillier::PubKey pk(keys.second);

    auto ctxts = encrypt(data, pk);
	auto res = mean(ctxts);
	std::vector<NTL::ZZ> slots;
	sk.Unpack(slots, res, 64);
	for (auto s : slots) {
		std::cout << s << " ";
	}
	std::cout << "\n";
    return 0;
}
