#include "algebra/Matrix.hpp"
#include "algebra/Vector.hpp"

#include <NTL/ZZX.h>
void test_Matrix()
{
    MDL::Matrix<double> mat(3, 3);

    mat[0][0] = 10;
    mat[1][1] = 3;
    mat[2][2] = 4;

    auto inv = mat.inverse();
    auto I   = inv.dot(mat);
    assert(I[0][0] == I[1][1] == I[2][2] == 1.0);
    assert(I[0][1] == I[1][2] == I[0][2] == 0.0);
}

void test_Vector()
{
    MDL::Vector<NTL::ZZX> vec(3);
    vec[0] = NTL::to_ZZX(1);
    vec[1] = NTL::to_ZZX(4);
    vec[2] = NTL::to_ZZX(3);
    assert(std::abs(vec.L2() - std::sqrt(26.0)) < 1e-9);
}

int main() {
    test_Matrix();
    test_Vector();
    printf("Passed all test!\n");
    return 0;
}