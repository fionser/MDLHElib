#include "PCA.hpp"
#include "algebra/NDSS.h"
#include "utils/timer.hpp"
#include "fhe/EncryptedArray.h"
#include "fhe/NumbTh.h"
namespace MDL {
std::pair<EncVector, EncVector> runPCA(const EncMatrix &mat,
                                       const EncryptedArray &ea,
                                       const FHEPubKey &pk,
                                       const long dimension)
{
    EncVector u(mat[0]), previousU(pk);
    // u0 = [1, 1, 1, ...., 1]
    // so u1 = M * u0 is no need to do mulplication.
    for (size_t r = 1; r < mat.size(); r++) u += mat[r];

    for (long it = 1; it < PCA::ITERATION; it++) {
        previousU = u;
        u = mat.column_dot(u, ea, dimension);
    }
    return {u, previousU};
}
} // namespace MDL