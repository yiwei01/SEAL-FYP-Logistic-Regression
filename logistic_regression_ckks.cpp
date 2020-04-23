#include <iostream>
#include <iomanip>
#include <fstream>
#include "seal/seal.h"

using namespace std;
using namespace seal;

// Helper function that prints parameters
void print_parameters(shared_ptr<SEALContext> context)
{
    // Verify parameters
    if (!context)
    {
        throw invalid_argument("context is not set");
    }
    auto &context_data = *context->key_context_data();

    string scheme_name;
    switch (context_data.parms().scheme())
    {
    case scheme_type::BFV:
        scheme_name = "BFV";
        break;
    case scheme_type::CKKS:
        scheme_name = "CKKS";
        break;
    default:
        throw invalid_argument("unsupported scheme");
    }
    cout << "/" << endl;
    cout << "| Encryption parameters :" << endl;
    cout << "|   scheme: " << scheme_name << endl;
    cout << "|   poly_modulus_degree: " << context_data.parms().poly_modulus_degree() << endl;

    cout << "|   coeff_modulus size: ";
    cout << context_data.total_coeff_modulus_bit_count() << " (";
    auto coeff_modulus = context_data.parms().coeff_modulus();
    size_t coeff_mod_count = coeff_modulus.size();
    for (size_t i = 0; i < coeff_mod_count - 1; i++)
    {
        cout << coeff_modulus[i].bit_count() << " + ";
    }
    cout << coeff_modulus.back().bit_count();
    cout << ") bits" << endl;

    if (context_data.parms().scheme() == scheme_type::BFV)
    {
        cout << "|   plain_modulus: " << context_data.parms().plain_modulus().value() << endl;
    }

    cout << "\\" << endl;
}

// Helper function that prints a matrix (vector of vectors)
template <typename T>
inline void print_full_matrix(vector<vector<T>> matrix, int precision = 3)
{
    // save formatting for cout
    ios old_fmt(nullptr);
    old_fmt.copyfmt(cout);
    cout << fixed << setprecision(precision);
    int row_size = matrix.size();
    int col_size = matrix[0].size();
    for (unsigned int i = 0; i < row_size; i++)
    {
        cout << "[";
        for (unsigned int j = 0; j < col_size - 1; j++)
        {
            cout << matrix[i][j] << ", ";
        }
        cout << matrix[i][col_size - 1];
        cout << "]" << endl;
    }
    cout << endl;
    // restore old cout formatting
    cout.copyfmt(old_fmt);
}

// Helper function that prints parts of a matrix (only squared matrix)
template <typename T>
inline void print_partial_matrix(vector<vector<T>> matrix, int print_size = 3, int precision = 3)
{
    // save formatting for cout
    ios old_fmt(nullptr);
    old_fmt.copyfmt(cout);
    cout << fixed << setprecision(precision);

    int row_size = matrix.size();
    int col_size = matrix[0].size();

    // Boundary check
    if (row_size < 2 * print_size && col_size < 2 * print_size)
    {
        cerr << "Cannot print matrix with these dimensions: " << to_string(row_size) << "x" << to_string(col_size) << ". Increase the print size" << endl;
        return;
    }
    // print first 4 elements
    for (unsigned int row = 0; row < print_size; row++)
    {
        cout << "\t[";
        for (unsigned int col = 0; col < print_size; col++)
        {
            cout << matrix[row][col] << ", ";
        }
        cout << "..., ";
        for (unsigned int col = col_size - print_size; col < col_size - 1; col++)
        {
            cout << matrix[row][col] << ", ";
        }
        cout << matrix[row][col_size - 1];
        cout << "]" << endl;
    }
    cout << "\t..." << endl;

    for (unsigned int row = row_size - print_size; row < row_size; row++)
    {
        cout << "\t[";
        for (unsigned int col = 0; col < print_size; col++)
        {
            cout << matrix[row][col] << ", ";
        }
        cout << "..., ";
        for (unsigned int col = col_size - print_size; col < col_size - 1; col++)
        {
            cout << matrix[row][col] << ", ";
        }
        cout << matrix[row][col_size - 1];
        cout << "]" << endl;
    }

    cout << endl;
    // restore old cout formatting
    cout.copyfmt(old_fmt);
}

template <typename T>
inline void print_partial_vector(vector<T> vec, int size, int print_size = 3, int precision = 3)
{
    // save formatting for cout
    ios old_fmt(nullptr);
    old_fmt.copyfmt(cout);
    cout << fixed << setprecision(precision);

    int row_size = size;

    // Boundary check
    if (row_size < 2 * print_size)
    {
        cerr << "Cannot print vector with these dimensions: " << to_string(row_size) << ". Increase the print size" << endl;
        return;
    }

    cout << "\t[";
    for (unsigned int row = 0; row < print_size; row++)
    {
        cout << vec[row] << ", ";
    }
    cout << "..., ";

    for (unsigned int row = row_size - print_size; row < row_size - 1; row++)
    {
        cout << vec[row] << ", ";
    }
    cout << vec[row_size - 1] << "]\n";

    cout << endl;
    // restore old cout formatting
    cout.copyfmt(old_fmt);
}

// Gets a diagonal from a matrix U
template <typename T>
vector<T> get_diagonal(int position, vector<vector<T>> U)
{

    vector<T> diagonal(U.size());

    int k = 0;
    // U(0,l) , U(1,l+1), ... ,  U(n-l-1, n-1)
    for (int i = 0, j = position; (i < U.size() - position) && (j < U.size()); i++, j++)
    {
        diagonal[k] = U[i][j];
        k++;
    }
    for (int i = U.size() - position, j = 0; (i < U.size()) && (j < position); i++, j++)
    {
        diagonal[k] = U[i][j];
        k++;
    }

    return diagonal;
}

template <typename T>
vector<vector<T>> get_all_diagonals(vector<vector<T>> U)
{

    vector<vector<T>> diagonal_matrix(U.size());

    for (int i = 0; i < U.size(); i++)
    {
        diagonal_matrix[i] = get_diagonal(i, U);
    }

    return diagonal_matrix;
}

Ciphertext Linear_Transform_Plain(Ciphertext ct, vector<Plaintext> U_diagonals, GaloisKeys gal_keys, EncryptionParameters params)
{
    auto context = SEALContext::Create(params);
    Evaluator evaluator(context);

    // Fill ct with duplicate
    Ciphertext ct_rot;
    evaluator.rotate_vector(ct, -U_diagonals.size(), gal_keys, ct_rot);
    // cout << "U_diagonals.size() = " << U_diagonals.size() << endl;
    Ciphertext ct_new;
    evaluator.add(ct, ct_rot, ct_new);

    vector<Ciphertext> ct_result(U_diagonals.size());
    evaluator.multiply_plain(ct_new, U_diagonals[0], ct_result[0]);

    for (int l = 1; l < U_diagonals.size(); l++)
    {
        Ciphertext temp_rot;
        evaluator.rotate_vector(ct_new, l, gal_keys, temp_rot);
        evaluator.multiply_plain(temp_rot, U_diagonals[l], ct_result[l]);
    }
    Ciphertext ct_prime;
    evaluator.add_many(ct_result, ct_prime);

    return ct_prime;
}

Ciphertext Linear_Transform_Cipher(Ciphertext ct, vector<Ciphertext> U_diagonals, GaloisKeys gal_keys, EncryptionParameters params)
{
    auto context = SEALContext::Create(params);
    Evaluator evaluator(context);

    // Fill ct with duplicate
    Ciphertext ct_rot;
    evaluator.rotate_vector(ct, -U_diagonals.size(), gal_keys, ct_rot);
    // cout << "U_diagonals.size() = " << U_diagonals.size() << endl;
    Ciphertext ct_new;
    evaluator.add(ct, ct_rot, ct_new);

    vector<Ciphertext> ct_result(U_diagonals.size());
    evaluator.multiply(ct_new, U_diagonals[0], ct_result[0]);

    for (int l = 1; l < U_diagonals.size(); l++)
    {
        Ciphertext temp_rot;
        evaluator.rotate_vector(ct_new, l, gal_keys, temp_rot);
        evaluator.multiply(temp_rot, U_diagonals[l], ct_result[l]);
    }
    Ciphertext ct_prime;
    evaluator.add_many(ct_result, ct_prime);

    return ct_prime;
}

// Linear transformation function between ciphertext matrix and plaintext vector
Ciphertext Linear_Transform_CipherMatrix_PlainVector(vector<Plaintext> pt_rotations, vector<Ciphertext> U_diagonals, GaloisKeys gal_keys, Evaluator &evaluator)
{
    vector<Ciphertext> ct_result(pt_rotations.size());

    for (int i = 0; i < pt_rotations.size(); i++)
    {
        evaluator.multiply_plain(U_diagonals[i], pt_rotations[i], ct_result[i]);
    }

    Ciphertext ct_prime;
    evaluator.add_many(ct_result, ct_prime);

    return ct_prime;
}

template <typename T>
vector<vector<double>> get_matrix_of_ones(int position, vector<vector<T>> U)
{
    vector<vector<double>> diagonal_of_ones(U.size(), vector<double>(U.size()));
    vector<T> U_diag = get_diagonal(position, U);

    int k = 0;
    for (int i = 0; i < U.size(); i++)
    {
        for (int j = 0; j < U.size(); j++)
        {
            if (U[i][j] == U_diag[k])
            {
                diagonal_of_ones[i][j] = 1;
            }
            else
            {
                diagonal_of_ones[i][j] = 0;
            }
        }
        k++;
    }

    return diagonal_of_ones;
}

// Encodes Ciphertext Matrix into a single vector (Row ordering of a matix)
Ciphertext C_Matrix_Encode(vector<Ciphertext> matrix, GaloisKeys gal_keys, EncryptionParameters params)
{
    auto context = SEALContext::Create(params);
    Evaluator evaluator(context);

    Ciphertext ct_result;
    int dimension = matrix.size();
    vector<Ciphertext> ct_rots(dimension);
    ct_rots[0] = matrix[0];

    for (int i = 1; i < dimension; i++)
    {
        evaluator.rotate_vector(matrix[i], (i * -dimension), gal_keys, ct_rots[i]);
    }

    evaluator.add_many(ct_rots, ct_result);

    return ct_result;
}

template <typename T>
vector<double> pad_zero(int offset, vector<T> U_vec)
{

    vector<double> result_vec(pow(U_vec.size(), 2));
    // Fill before U_vec
    for (int i = 0; i < offset; i++)
    {
        result_vec[i] = 0;
    }
    // Fill U_vec
    for (int i = 0; i < U_vec.size(); i++)
    {
        result_vec[i + offset] = U_vec[i];
    }
    // Fill after U_vec
    for (int i = offset + U_vec.size(); i < result_vec.size(); i++)
    {
        result_vec[i] = 0;
    }
    return result_vec;
}

// U_transpose
template <typename T>
vector<vector<double>> get_U_transpose(vector<vector<T>> U)
{

    int dimension = U.size();
    int dimensionSq = pow(dimension, 2);
    vector<vector<double>> U_transpose(dimensionSq, vector<double>(dimensionSq));

    int tranposed_row = 0;

    for (int i = 0; i < dimension; i++)
    {
        // Get matrix of ones at position k
        vector<vector<double>> one_matrix = get_matrix_of_ones(i, U);
        print_full_matrix(one_matrix);

        // Loop over matrix of ones
        for (int offset = 0; offset < dimension; offset++)
        {
            vector<double> temp_fill = pad_zero(offset * dimension, one_matrix[0]);

            U_transpose[tranposed_row] = temp_fill;
            tranposed_row++;
        }
    }

    return U_transpose;
}

void compute_all_powers(const Ciphertext &ctx, int degree, Evaluator &evaluator, RelinKeys &relin_keys, vector<Ciphertext> &powers)
{

    powers.resize(degree + 1);
    powers[1] = ctx;

    vector<int> levels(degree + 1, 0);
    levels[1] = 0;
    levels[0] = 0;

    for (int i = 2; i <= degree; i++)
    {
        // compute x^i
        int minlevel = i;
        int cand = -1;
        for (int j = 1; j <= i / 2; j++)
        {
            int k = i - j;
            //
            int newlevel = max(levels[j], levels[k]) + 1;
            if (newlevel < minlevel)
            {
                cand = j;
                minlevel = newlevel;
            }
        }
        levels[i] = minlevel;
        // use cand
        if (cand < 0)
            throw runtime_error("error");
        //cout << "levels " << i << " = " << levels[i] << endl;
        // cand <= i - cand by definition
        Ciphertext temp = powers[cand];
        evaluator.mod_switch_to_inplace(temp, powers[i - cand].parms_id());

        evaluator.multiply(temp, powers[i - cand], powers[i]);
        evaluator.relinearize_inplace(powers[i], relin_keys);
        evaluator.rescale_to_next_inplace(powers[i]);
    }
    return;
}

// Tree method for polynomial evaluation
void tree(int degree, double x)
{
    chrono::high_resolution_clock::time_point time_start, time_end;
    chrono::microseconds time_diff;

    EncryptionParameters parms(scheme_type::CKKS);

    int depth = ceil(log2(degree));

    vector<int> moduli(depth + 4, 40);
    moduli[0] = 50;
    moduli[moduli.size() - 1] = 59;

    size_t poly_modulus_degree = 16384;
    parms.set_poly_modulus_degree(poly_modulus_degree);
    parms.set_coeff_modulus(CoeffModulus::Create(
        poly_modulus_degree, moduli));

    double scale = pow(2.0, 40);

    auto context = SEALContext::Create(parms);

    KeyGenerator keygen(context);
    auto pk = keygen.public_key();
    auto sk = keygen.secret_key();
    auto relin_keys = keygen.relin_keys();
    Encryptor encryptor(context, pk);
    Decryptor decryptor(context, sk);

    Evaluator evaluator(context);
    CKKSEncoder ckks_encoder(context);

    print_parameters(context);
    cout << endl;

    Plaintext ptx;
    ckks_encoder.encode(x, scale, ptx);
    Ciphertext ctx;
    encryptor.encrypt(ptx, ctx);
    cout << "x = " << x << endl;

    vector<double> coeffs(degree + 1);
    vector<Plaintext> plain_coeffs(degree + 1);

    // Random Coefficients from 0-1
    cout << "Polynomial = ";
    int counter = 0;
    for (size_t i = 0; i < degree + 1; i++)
    {
        coeffs[i] = (double)rand() / RAND_MAX;
        ckks_encoder.encode(coeffs[i], scale, plain_coeffs[i]);
        cout << "x^" << counter << " * (" << coeffs[i] << ")"
             << ", ";
    }
    cout << endl;

    Plaintext plain_result;
    vector<double> result;

    /*
    decryptor.decrypt(ctx, plain_result);
    ckks_encoder.decode(plain_result, result);
    cout << "ctx  = " << result[0] << endl;
    */

    double expected_result = coeffs[degree];

    // Compute all powers
    vector<Ciphertext> powers(degree + 1);

    time_start = chrono::high_resolution_clock::now();

    compute_all_powers(ctx, degree, evaluator, relin_keys, powers);
    cout << "All powers computed " << endl;

    Ciphertext enc_result;
    // result = a[0]
    cout << "Encrypt first coeff...";
    encryptor.encrypt(plain_coeffs[0], enc_result);
    cout << "Done" << endl;

    /*
    for (int i = 1; i <= degree; i++){
        decryptor.decrypt(powers[i], plain_result);
        ckks_encoder.decode(plain_result, result);
        // cout << "power  = " << result[0] << endl;
    }
    */

    Ciphertext temp;

    // result += a[i]*x[i]
    for (int i = 1; i <= degree; i++)
    {

        // cout << i << "-th sum started" << endl;
        evaluator.mod_switch_to_inplace(plain_coeffs[i], powers[i].parms_id());
        evaluator.multiply_plain(powers[i], plain_coeffs[i], temp);

        evaluator.rescale_to_next_inplace(temp);
        evaluator.mod_switch_to_inplace(enc_result, temp.parms_id());

        // Manual Rescale
        enc_result.scale() = pow(2.0, 40);
        temp.scale() = pow(2.0, 40);

        evaluator.add_inplace(enc_result, temp);
        // cout << i << "-th sum done" << endl;
    }

    time_end = chrono::high_resolution_clock::now();
    time_diff = chrono::duration_cast<chrono::microseconds>(time_end - time_start);
    cout << "Evaluation Duration:\t" << time_diff.count() << " microseconds" << endl;

    // Compute Expected result
    for (int i = degree - 1; i >= 0; i--)
    {
        expected_result *= x;
        expected_result += coeffs[i];
    }

    decryptor.decrypt(enc_result, plain_result);
    ckks_encoder.decode(plain_result, result);

    cout << "Actual : " << result[0] << "\nExpected : " << expected_result << "\ndiff : " << abs(result[0] - expected_result) << endl;

    // TEST Garbage
}

template <typename T>
vector<T> rotate_vec(vector<T> input_vec, int num_rotations)
{
    if (num_rotations > input_vec.size())
    {
        cerr << "Invalid number of rotations" << endl;
        exit(EXIT_FAILURE);
    }

    vector<T> rotated_res(input_vec.size());
    for (int i = 0; i < input_vec.size(); i++)
    {
        rotated_res[i] = input_vec[(i + num_rotations) % (input_vec.size())];
    }

    return rotated_res;
}

Ciphertext sigmoid(Ciphertext ct)
{
    Ciphertext res;
    // -------- write polynomial approximation --------
    return res;
}

Ciphertext predict(vector<Ciphertext> features, Plaintext weights, int num_weights, double scale, Evaluator &evaluator, CKKSEncoder &ckks_encoder, GaloisKeys gal_keys)
{
    // Get rotations of weights
    vector<Plaintext> weights_rotations(num_weights);
    weights_rotations[0] = weights;

    vector<double> decoded_weights(num_weights);
    ckks_encoder.decode(weights, decoded_weights);

    for (int i = 1; i < num_weights; i++)
    {
        // rotate
        vector<double> rotated_vec = rotate_vec(decoded_weights, i);

        // encode
        Plaintext pt;
        ckks_encoder.encode(rotated_vec, scale, pt);

        // store
        weights_rotations[i] = pt;
    }

    // Linear Transformation
    Ciphertext lintransf_vec = Linear_Transform_CipherMatrix_PlainVector(weights_rotations, features, gal_keys, evaluator);

    // Sigmoid over result
    Ciphertext predict_res = sigmoid(lintransf_vec);

    return predict_res;
}

int main()
{

    return 0;
}