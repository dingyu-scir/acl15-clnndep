#include "FSC_Classifier.h"
#include "Util.h"

#include <chrono>
#include "ThreadPool.h"

#include "fastexp.h"

#include <cstdlib>
#include <ctime>
#include <cmath>
#include <cassert>
#include <set>

#include <omp.h>

using namespace std;


// TODO Bug: fix_embedding

/**
 * Definition of static variables
 */
Mat<double> NNClassifier::grad_saved;
Mat<double> NNClassifier::saved;
unordered_map<int, int> NNClassifier::pre_map;

Mat<double> NNClassifier::W1;
Vec<double> NNClassifier::b1;
Mat<double> NNClassifier::W2;

Mat<double> NNClassifier::Wv;
Mat3<double> NNClassifier::Wr;

Mat<double> NNClassifier::Eb;
Mat<double> NNClassifier::Ed;
Mat<double> NNClassifier::Ev;
Mat<double> NNClassifier::Ec;

const vector< unordered_map<string, int> > NNClassifier::compose_structure = {
    {
        // word tokens
        {"w_lc1", 6},
        {"w_lc1lc1", 10},
        {"w_lc2", 8},
        {"w_root", 2},
        {"w_rc1", 7},
        {"w_rc1rc1", 11},
        {"w_rc2", 9},
        // label tokens (work tokens + 30)
        {"l_lc1", 36},
        {"l_lc1lc1", 40},
        {"l_lc2", 38},
        {"l_rc1", 37},
        {"l_rc1rc1", 41},
        {"l_rc2", 39},
    },
    {
        // word tokens
        {"w_lc1", 12},
        {"w_lc1lc1", 16},
        {"w_lc2", 14},
        {"w_root", 1},
        {"w_rc1", 13},
        {"w_rc1rc1", 17},
        {"w_rc2", 15},
        // label tokens (work tokens + 30)
        {"l_lc1", 42},
        {"l_lc1lc1", 46},
        {"l_lc2", 44},
        {"l_rc1", 43},
        {"l_rc1rc1", 47},
        {"l_rc2", 45},
    }
};

Dataset NNClassifier::dataset;


NNClassifier::NNClassifier()
{
}

NNClassifier::NNClassifier(const NNClassifier & classifier)
{
    config = classifier.config;

    // dataset = classifier.dataset;
    // E = classifier.E;
    // W1 = classifier.W1;
    // b1 = classifier.b1;
    // W2 = classifier.W2;
    // pre_map = classifier.pre_map;
    // grad_saved = classifier.grad_saved;
    // grad_saved.resize(pre_map.size(), config.hidden_size);
    // saved = classifier.saved;

    num_labels = classifier.num_labels;
    // debug = classifier.debug;
}

NNClassifier::NNClassifier(
        const Config& _config,
        const Mat<double>& _Eb,
        const Mat<double>& _Ed,
        const Mat<double>& _Ev,
        const Mat<double>& _Ec,
        const Mat<double>& _W1,
        const Vec<double>& _b1,
        const Mat<double>& _W2,
        const Mat<double>& _Wv,
        const Mat3<double>& _Wr,
        const vector<int>& pre_computed_ids)
{
    // NNClassifier(_config, Dataset(), _E, _W1, _b1, _W2, pre_computed_ids);
    config = _config;
    Eb = _Eb;
    Ed = _Ed;
    Ev = _Ev;
    Ec = _Ec;
    W1 = _W1;
    b1 = _b1;
    W2 = _W2;
    Wv = _Wv;
    Wr = _Wr;

    num_labels = W2.nrows();

    cursor = 0;

    // /* debug
    for (size_t i = 0; i < pre_computed_ids.size(); ++i)
    {
        pre_map[pre_computed_ids[i]] = i;
    }
    // */
}

NNClassifier::NNClassifier(
        const Config& _config,
        const Dataset& _dataset,
        const Mat<double>& _Eb,
        const Mat<double>& _Ed,
        const Mat<double>& _Ev,
        const Mat<double>& _Ec,
        const Mat<double>& _W1,
        const Vec<double>& _b1,
        const Mat<double>& _W2,
        const Mat<double>& _Wv,
        const Mat3<double>& _Wr,
        const vector<int>& pre_computed_ids)
{
    config = _config;
    dataset = _dataset;
    Eb = _Eb;
    Ed = _Ed;
    Ev = _Ev;
    Ec = _Ec;
    W1 = _W1;
    b1 = _b1;
    W2 = _W2;
    Wv = _Wv;
    Wr = _Wr;

    init_gradient_histories();

    num_labels = W2.nrows(); // number of transitions

    cursor = 0;

    // /* debug
    for (size_t i = 0; i < pre_computed_ids.size(); ++i)
    {
        pre_map[pre_computed_ids[i]] = i;
    }
    // */

    print_info();

    /*
    grad_W1.resize(W1.nrows(), W1.ncols());
    grad_b1.resize(b1.size());
    grad_W2.resize(W2.nrows(), W2.ncols());
    grad_E.resize(E.nrows(), E.ncols());
    */
    grad_saved.resize(pre_map.size(), config.hidden_size);

    // debug = true; // important when debug
}

void NNClassifier::set_dataset(
        const Dataset & _dataset,
        const vector<int> & pre_computed_ids)
{
    dataset = _dataset;

    init_gradient_histories();

    num_labels = W2.nrows(); // number of transitions

    cursor = 0;

    // /* debug
    for (size_t i = 0; i < pre_computed_ids.size(); ++i)
    {
        pre_map[pre_computed_ids[i]] = i;
    }
    // */

    print_info();

    grad_saved.resize(pre_map.size(), config.hidden_size);
}

Cost NNClassifier::thread_proc(vector<Sample> & chunk, size_t batch_size)
{
    Mat<double> grad_W1(0.0, W1.nrows(), W1.ncols());
    Vec<double> grad_b1(0.0, b1.size());
    Mat<double> grad_W2(0.0, W2.nrows(), W2.ncols());
    Mat<double> grad_Eb(0.0, Eb.nrows(), Eb.ncols());
    Mat<double> grad_Ed(0.0, Ed.nrows(), Ed.ncols());
    Mat<double> grad_Ev(0.0, Ev.nrows(), Ev.ncols());
    Mat<double> grad_Ec(0.0, Ec.nrows(), Ec.ncols());

    Mat<double> grad_Wv(0.0, Wv.nrows(), Wv.ncols());
    Mat3<double> grad_Wr(0.0, Wr.dim1(), Wr.dim2(), Wr.dim3());

    /*
    cerr << "W1.size = " << W1.nrows() << ", " << W1.ncols() << endl;
    cerr << "W2.size = " << W2.nrows() << ", " << W2.ncols() << endl;
    cerr << "b1.size = " << b1.size() << endl;
    cerr << "E.size = " << E.nrows() << ", " << E.ncols() << endl;

    cerr << "W1[0][1] = " << W1[0][1] << endl;
    cerr << "W2[0][1] = " << W2[0][1] << endl;
    cerr << "b1[1] = " << b1[1] << endl;
    cerr << "E[0][1] = " << E[0][1] << endl;
    */

    double loss = 0.0;
    int correct = 0;

    int Eb_label_start = Eb.nrows() - Wr.dim1(); // important

    vector< vector<int> > dropout_histories;

    for (size_t i = 0; i < chunk.size(); ++i)
    {
        vector<int>& features = chunk[i].get_feature();

        vector<int>& label = chunk[i].get_label();

        // feed forward the neural net
        Vec<double> scores(0.0, num_labels);
        Vec<double> hidden(0.0, config.hidden_size);
        Vec<double> hidden3(0.0, config.hidden_size);

        // Run dropout: randomly dropout some hidden units
        vector<int> active_units;
        dropout(config.hidden_size, config.dropout_prob, active_units);

        if (config.debug)
            dropout_histories.push_back(active_units);

        // feed forward the basic feature tokens to hidden layer
        int offset = 0;
        for (int j = 0; j < config.num_tokens; ++j)
        {
            int tok = features[j]; // feature ID
            int E_index = tok;
            // feature index in @pre_map.keys()
            // considering position in input layer
            int index = tok * config.num_tokens + j;
            int feat_type = config.get_feat_type(j);

            assert (feat_type != Config::NONEXIST);
            if (feat_type == Config::DIST_FEAT)
                E_index -= Eb.nrows();
            else if (feat_type == Config::VALENCY_FEAT)
                E_index -= Eb.nrows() + Ed.nrows();
            else if (feat_type == Config::CLUSTER_FEAT)
                E_index -= Eb.nrows() + Ed.nrows() + Ev.nrows();

            int emb_size = config.get_embedding_size(feat_type);
            // embedding size for current token

            // /* debug
            if (pre_map.find(index) != pre_map.end())
            {
                int id = pre_map[index];

                for (size_t k = 0; k < active_units.size(); ++k)
                {
                    int node_index = active_units[k]; // active hidden unit
                    hidden[node_index] += saved[id][node_index];
                }
            }
            else
            {
            // */
                for (size_t k = 0; k < active_units.size(); ++k)
                {
                    int node_index = active_units[k];
                    if (feat_type == Config::BASIC_FEAT)
                    {
                        for (int l = 0; l < emb_size; ++l)
                            hidden[node_index] +=
                                W1[node_index][offset+l] * Eb[E_index][l];
                    }
                    else if (feat_type == Config::DIST_FEAT)
                    {
                        for (int l = 0; l < emb_size; ++l)
                            hidden[node_index] +=
                                W1[node_index][offset+l] * Ed[E_index][l];
                    }
                    else if (feat_type == Config::VALENCY_FEAT)
                    {
                        for (int l = 0; l < emb_size; ++l)
                            hidden[node_index] +=
                                W1[node_index][offset+l] * Ev[E_index][l];
                    }
                    else if (feat_type == Config::CLUSTER_FEAT)
                    {
                        for (int l = 0; l < emb_size; ++l)
                            hidden[node_index] +=
                                W1[node_index][offset+l] * Ec[E_index][l];
                    }
                }
            }
            // offset += config.embedding_size;
            offset += emb_size;
        }

        Mat<double> llc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> lc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> rrc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> rc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);

        Mat<double> lc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> rc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> root_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);

        // feed forward the composed tokens to hidden layer
        for (int j = 0; j < config.num_compose_tokens; ++j)
        {
            // cerr << "Feed-forward the composition layers " << j << endl;
            // j = 0 | 1
            const unordered_map<string, int> tree = compose_structure[j];

            int tok_w_root = features[tree.find("w_root")->second]; // -> idx_w_root
            int tok_w_lc1  = features[tree.find("w_lc1")->second];
            int tok_w_lc2  = features[tree.find("w_lc2")->second];
            int tok_w_rc1  = features[tree.find("w_rc1")->second];
            int tok_w_rc2  = features[tree.find("w_rc2")->second];
            int tok_w_llc1 = features[tree.find("w_lc1lc1")->second];
            int tok_w_rrc1 = features[tree.find("w_rc1rc1")->second];

            int tok_l_lc1  = features[tree.find("l_lc1")->second];
            int tok_l_lc2  = features[tree.find("l_lc2")->second];
            int tok_l_rc1  = features[tree.find("l_rc1")->second];
            int tok_l_rc2  = features[tree.find("l_rc2")->second];
            int tok_l_llc1 = features[tree.find("l_lc1lc1")->second];
            int tok_l_rrc1 = features[tree.find("l_rc1rc1")->second];

            int idx_l_lc1  = tok_l_lc1 - Eb_label_start;
            int idx_l_lc2  = tok_l_lc2 - Eb_label_start;
            int idx_l_rc1  = tok_l_rc1 - Eb_label_start;
            int idx_l_rc2  = tok_l_rc2 - Eb_label_start;
            int idx_l_llc1 = tok_l_llc1 - Eb_label_start;
            int idx_l_rrc1 = tok_l_rrc1 - Eb_label_start;

            // propagate leaf nodes (embedding) to hidden layers

            // in contrast to the cube activation function
            //  for sigmoidal functions, we don't have to store
            //  all hidden outputs before activation (W * x + b)
            //  So, we can just overwrite the hidden layer to store
            //  the activated output.
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                for (int l = 0; l < Eb.ncols(); ++l)
                {
                    llc1_hidden[j][k] += Wv[k][l] * Eb[tok_w_llc1][l];
                    rrc1_hidden[j][k] += Wv[k][l] * Eb[tok_w_rrc1][l];

                    lc2_hidden[j][k]  += Wv[k][l] * Eb[tok_w_lc2][l];
                    rc2_hidden[j][k]  += Wv[k][l] * Eb[tok_w_rc2][l];

                    lc1_hidden[j][k]  += Wv[k][l] * Eb[tok_w_lc1][l];
                    rc1_hidden[j][k]  += Wv[k][l] * Eb[tok_w_rc1][l];

                    root_hidden[j][k] += Wv[k][l] * Eb[tok_w_root][l];
                }
            }
            // non-linear activation for llc1, rrc1, lc2, rc2
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                llc1_hidden[j][k] = tanh(llc1_hidden[j][k]);
                rrc1_hidden[j][k] = tanh(rrc1_hidden[j][k]);
                lc2_hidden[j][k] = tanh(lc2_hidden[j][k]);
                rc2_hidden[j][k] = tanh(rc2_hidden[j][k]);
            }
            // bias

            // compose llc1+lc1->lc1_hidden
            // compose rrc1+rc1->rc1_hidden
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    lc1_hidden[j][k] += Wr[idx_l_llc1][k][l] * llc1_hidden[j][l];
                    rc1_hidden[j][k] += Wr[idx_l_rrc1][k][l] * rrc1_hidden[j][l];
                }
            }
            // non-linear activation for lc1, lc2
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                lc1_hidden[j][k] = tanh(lc1_hidden[j][k]);
                rc1_hidden[j][k] = tanh(rc1_hidden[j][k]);
            }

            // propagate lc1+lc2+rc2+rc1 -> w_root
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    root_hidden[j][k] += Wr[idx_l_lc1][k][l] * lc1_hidden[j][l] // lc1
                                    + Wr[idx_l_lc2][k][l] * lc2_hidden[j][l] // lc2
                                    + Wr[idx_l_rc2][k][l] * rc2_hidden[j][l] // rc2
                                    + Wr[idx_l_rc1][k][l] * rc1_hidden[j][l];// rc1
                }
            }
            // non-linear activation for w_root
            for (int k = 0; k < config.compose_embedding_size; ++k)
                root_hidden[j][k] = tanh(root_hidden[j][k]);
            // bias

            // propagate the composed root -> hidden
            for (size_t k = 0; k < active_units.size(); ++k)
            {
                int node_index = active_units[k];
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    hidden[node_index] +=
                        W1[node_index][offset+l] * root_hidden[j][l];
                }
            }
            offset += config.compose_embedding_size;
        }

        // add bias term
        // activate
        for (size_t j = 0; j < active_units.size(); ++j)
        {
            int node_index = active_units[j];
            hidden[node_index] += b1[node_index];
            // cube active function
            hidden3[node_index] = pow(hidden[node_index], 3);
            // clip: not sure whether it's right
            /*
            if (hidden3[node_index] > 50)
                hidden3[node_index] = 50;
            if (hidden3[node_index] < -50)
                hidden3[node_index] = -50;
            */
        }

        /*
        cerr << "hidden: " << endl;
        for (int j = 0; j < hidden.size(); ++j)
            cerr << hidden[j] << " ";
        cerr << endl;

        cerr << "hidden3: " << endl;
        for (int j = 0; j < hidden.size(); ++j)
            cerr << hidden3[j] << " ";
        cerr << endl;
        */

        // feed forward to softmax layer
        int opt_label = -1;
        for (int j = 0; j < num_labels; ++j)
        {
            for (size_t k = 0; k < active_units.size(); ++k)
            {
                int node_index = active_units[k];
                scores[j] += W2[j][node_index] * hidden3[node_index];
            }
            if (label[j] >= 0)
                if (opt_label < 0 || scores[j] > scores[opt_label])
                    opt_label = j;
        }

        /*
        cerr << "unnormalized scores: " << endl;
        for (int j = 0; j < scores.size(); ++j)
            cerr << scores[j] << " ";
        cerr << endl;
        */

        double sum1 = .0;
        double sum2 = .0;
        double max_score = scores[opt_label];
        Vec<double> tmp = scores;
        for (int j = 0; j < num_labels; ++j)
        {
            if (label[j] >= 0)
            {
                scores[j] = fasterexp(scores[j] - max_score);
                // scores[j] = exp(scores[j] - max_score);
                if (label[j] == 1) sum1 += scores[j];
                sum2 += scores[j];
            }
        }
        if (sum1 == 0)
        {
            cerr << "Original: " << endl;
            for (int j = 0; j < scores.size(); ++j)
                cerr << label[j] << ": " << tmp[j] << ", ";
            cerr << endl;
            cerr << "opt_label = " << opt_label << endl;
            cerr << "max_score = " << max_score << endl;
            for (int j = 0; j < scores.size(); ++j)
                cerr << label[j] << ": " << scores[j] << ", ";
            cerr << endl;
        }

        /*
        cerr << "normalized scores: " << endl;
        for (int j = 0; j < scores.size(); ++j)
            cerr << scores[j] << " ";
        cerr << endl;

        // cerr << "label = " << label << " | " << num_labels << endl;
        cerr << "sum1 = " << sum1 << endl;
        cerr << "sum2 = " << sum2 << endl;
        cerr << "add to cost: (" << log(sum2) << " - " << log(sum1) << ")" << endl;
        */
        loss += (log(sum2) - log(sum1)); // divide batch_size
        if (label[opt_label] == 1)
            correct += 1; // divide batch_size

        // compute the gradients
        // here, we only consider the situation where only one unit
        // in the output layer is activated.
        // NB: in Danqi's implementation, she consider all possible decisions
        Vec<double> grad_hidden3(0.0, config.hidden_size);
        // double delta = -(1 - scores[label] / sum2) / config.batch_size;

        for (int i = 0; i < num_labels; ++i)
        {
            if (label[i] >= 0)
            {
                double delta = -(label[i] - scores[i] / sum2) / batch_size;
                for (size_t j = 0; j < active_units.size(); ++j)
                {
                    int node_index = active_units[j];
                    grad_W2[i][node_index] += delta * hidden3[node_index];
                    grad_hidden3[node_index] += delta * W2[i][node_index];
                }
            }
        }

        Vec<double> grad_hidden(0.0, config.hidden_size);
        // #pragma omp parallel for
        for (size_t j = 0; j < active_units.size(); ++j)
        {
            int node_index = active_units[j];
            grad_hidden[node_index] = grad_hidden3[node_index]
                                        * 3
                                        * hidden[node_index]
                                        * hidden[node_index];
            grad_b1[node_index] += grad_hidden[node_index];
        }

        offset = 0;
        for (int j = 0; j < config.num_tokens; ++j)
        {
            int tok = features[j];
            int E_index = tok;
            int index = tok * config.num_tokens + j;
            int feat_type = config.get_feat_type(j);

            assert (feat_type != Config::NONEXIST);
            if (feat_type == Config::DIST_FEAT)
                E_index -= Eb.nrows();
            else if (feat_type == Config::VALENCY_FEAT)
                E_index -= Eb.nrows() + Ed.nrows();
            else if (feat_type == Config::CLUSTER_FEAT)
                E_index -= Eb.nrows() + Ed.nrows() + Ev.nrows();

            int emb_size = config.get_embedding_size(feat_type);
            // /* debug
            if (pre_map.find(index) != pre_map.end())
            {
                int id = pre_map[index];
                for (size_t k = 0; k < active_units.size(); ++k)
                {
                    int node_index = active_units[k];
                    grad_saved[id][node_index] += grad_hidden[node_index];
                }
            }
            else
            {
            // */
                for (size_t k = 0; k < active_units.size(); ++k)
                {
                    int node_index = active_units[k];
                    for (int l = 0; l < emb_size; ++l)
                    {
                        if (feat_type == Config::BASIC_FEAT)
                        {
                            grad_W1[node_index][offset+l] +=
                                grad_hidden[node_index] * Eb[E_index][l];
                            grad_Eb[E_index][l] +=
                                grad_hidden[node_index] * W1[node_index][offset+l];
                        }
                        else if (feat_type == Config::DIST_FEAT)
                        {
                            grad_W1[node_index][offset+l] +=
                                grad_hidden[node_index] * Ed[E_index][l];
                            grad_Ed[E_index][l] +=
                                grad_hidden[node_index] * W1[node_index][offset+l];
                        }
                        else if (feat_type == Config::VALENCY_FEAT)
                        {
                            grad_W1[node_index][offset+l] +=
                                grad_hidden[node_index] * Ev[E_index][l];
                            grad_Ev[E_index][l] +=
                                grad_hidden[node_index] * W1[node_index][offset+l];
                        }
                        else if (feat_type == Config::CLUSTER_FEAT)
                        {
                            grad_W1[node_index][offset+l] +=
                                grad_hidden[node_index] * Ec[E_index][l];
                            grad_Ec[E_index][l] +=
                                grad_hidden[node_index] * W1[node_index][offset+l];
                        }

                    }
                }
            }
            offset += emb_size;
        }

        Mat<double> grad_root_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> grad_llc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> grad_rrc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> grad_lc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> grad_lc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> grad_rc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> grad_rc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);

        // Back-propagation to the composition layers
        for (int j = 0; j < config.num_compose_tokens; ++j)
        {
            // cerr << "back-propagation to the composition layer" << endl;
            const unordered_map<string, int> tree = compose_structure[j];

            int tok_w_root = features[tree.find("w_root")->second];
            int tok_w_lc1  = features[tree.find("w_lc1")->second];
            int tok_w_lc2  = features[tree.find("w_lc2")->second];
            int tok_w_rc1  = features[tree.find("w_rc1")->second];
            int tok_w_rc2  = features[tree.find("w_rc2")->second];
            int tok_w_llc1 = features[tree.find("w_lc1lc1")->second];
            int tok_w_rrc1 = features[tree.find("w_rc1rc1")->second];

            int tok_l_lc1  = features[tree.find("l_lc1")->second];
            int tok_l_lc2  = features[tree.find("l_lc2")->second];
            int tok_l_rc1  = features[tree.find("l_rc1")->second];
            int tok_l_rc2  = features[tree.find("l_rc2")->second];
            int tok_l_llc1 = features[tree.find("l_lc1lc1")->second];
            int tok_l_rrc1 = features[tree.find("l_rc1rc1")->second];

            int idx_l_lc1  = tok_l_lc1 - Eb_label_start;
            int idx_l_lc2  = tok_l_lc2 - Eb_label_start;
            int idx_l_rc1  = tok_l_rc1 - Eb_label_start;
            int idx_l_rc2  = tok_l_rc2 - Eb_label_start;
            int idx_l_llc1 = tok_l_llc1 - Eb_label_start;
            int idx_l_rrc1 = tok_l_rrc1 - Eb_label_start;


            // compute grad_root_hidden
            for (size_t k = 0; k < active_units.size(); ++k)
            {
                int node_index = active_units[k];
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    grad_W1[node_index][offset+l] += grad_hidden[node_index] * root_hidden[j][l];
                    grad_root_hidden[j][l] += grad_hidden[node_index] * W1[node_index][offset+l];
                }
            }

            // compute grad_lc1_hidden, grad_rc1_hidden
            //         grad_lc2_hidden, grad_rc2_hidden
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                // gradient of tanh(x)
                double grad_root_net = grad_root_hidden[j][k]
                                     * (1 - root_hidden[j][k] * root_hidden[j][k]);
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    grad_Wr[idx_l_lc1][k][l] += grad_root_net * lc1_hidden[j][l];
                    grad_lc1_hidden[j][l] += grad_root_net * Wr[idx_l_lc1][k][l];

                    grad_Wr[idx_l_lc2][k][l] += grad_root_net * lc2_hidden[j][l];
                    grad_lc2_hidden[j][l] += grad_root_net * Wr[idx_l_lc2][k][l];

                    grad_Wr[idx_l_rc2][k][l] += grad_root_net * rc2_hidden[j][l];
                    grad_rc2_hidden[j][l] += grad_root_net * Wr[idx_l_rc2][k][l];

                    grad_Wr[idx_l_rc1][k][l] += grad_root_net * rc1_hidden[j][l];
                    grad_rc1_hidden[j][l] += grad_root_net * Wr[idx_l_rc1][k][l];
                }
                for (int l = 0; l < config.embedding_size; ++l)
                {
                    grad_Wv[k][l] += grad_root_net * Eb[tok_w_root][l];
                    grad_Eb[tok_w_root][l] += grad_root_net * Wv[k][l];
                }
            }

            // compute grad_llc1_hidden and grad(lc1)
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                double grad_lc1_net = grad_lc1_hidden[j][k]
                                    * (1 - lc1_hidden[j][k] * lc1_hidden[j][k]);
                // llc1_hidden
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    grad_Wr[idx_l_llc1][k][l] += grad_lc1_net * llc1_hidden[j][l];
                    grad_llc1_hidden[j][l] += grad_lc1_net * Wr[idx_l_llc1][k][l];
                }
                // lc1
                for (int l = 0; l < config.embedding_size; ++l)
                {
                    grad_Wv[k][l] += grad_lc1_net * Eb[tok_w_lc1][l];
                    grad_Eb[tok_w_lc1][l] += grad_lc1_net * Wv[k][l];
                }
            }

            // compute grad(lc2)
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                double grad_lc2_net = grad_lc2_hidden[j][k]
                                    * (1 - lc2_hidden[j][k] * lc2_hidden[j][k]);
                // lc2
                for (int l = 0; l < config.embedding_size; ++l)
                {
                    grad_Wv[k][l] += grad_lc2_net * Eb[tok_w_lc2][l];
                    grad_Eb[tok_w_lc2][l] += grad_lc2_net * Wv[k][l];
                }
            }

            // compute grad(rc2)
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                double grad_rc2_net = grad_rc2_hidden[j][k]
                                    * (1 - rc2_hidden[j][k] * rc2_hidden[j][k]);
                // rc2
                for (int l = 0; l < config.embedding_size; ++l)
                {
                    grad_Wv[k][l] += grad_rc2_net * Eb[tok_w_rc2][l];
                    grad_Eb[tok_w_rc2][l] += grad_rc2_net * Wv[k][l];
                }
            }

            // compute grad_rc1_hidden and grad(rc1)
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                double grad_rc1_net = grad_rc1_hidden[j][k]
                                    * (1 - rc1_hidden[j][k] * rc1_hidden[j][k]);
                // rc1
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    grad_Wr[idx_l_rrc1][k][l] += grad_rc1_net * rrc1_hidden[j][l];
                    grad_rrc1_hidden[j][l] += grad_rc1_net * Wr[idx_l_rrc1][k][l];
                }
                for (int l = 0; l < config.embedding_size; ++l)
                {
                    grad_Wv[k][l] += grad_rc1_net * Eb[tok_w_rc1][l];
                    grad_Eb[tok_w_rc1][l] += grad_rc1_net * Wv[k][l];
                }
            }

            // compute grad_(llc1)
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                double grad_llc1_net = grad_llc1_hidden[j][k]
                                     * (1 - llc1_hidden[j][k] * llc1_hidden[j][k]);
                for (int l = 0; l < config.embedding_size; ++l)
                {
                    grad_Wv[k][l] += grad_llc1_net * Eb[tok_w_llc1][l];
                    grad_Eb[tok_w_llc1][l] += grad_llc1_net * Wv[k][l];
                }
            }

            // compute grad_(rrc1)
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                double grad_rrc1_net = grad_rrc1_hidden[j][k]
                                     * (1 - rrc1_hidden[j][k] * rrc1_hidden[j][k]);
                for (int l = 0; l < config.embedding_size; ++l)
                {
                    grad_Wv[k][l] += grad_rrc1_net * Eb[tok_w_rrc1][l];
                    grad_Eb[tok_w_rrc1][l] += grad_rrc1_net * Wv[k][l];
                }
            }

            // compute grad
            offset += config.compose_embedding_size;
        }
    }

    // cerr << "Thread loss = " << loss << endl;
    loss /= batch_size;
    double accuracy = (double)correct / batch_size;

    Cost cost(loss,
                accuracy,
                grad_W1,
                grad_b1,
                grad_W2,
                grad_Eb,
                grad_Ed,
                grad_Ev,
                grad_Ec,
                grad_Wv,
                grad_Wr,
                dropout_histories);

    /*
    cerr << "grad_W2: " << grad_W2.nrows() << " * " << grad_W2.ncols() << endl
         << "grad_W1: " << grad_W1.nrows() << " * " << grad_W1.ncols() << endl
         << "grad_Eb: " << grad_Eb.nrows() << " * " << grad_Eb.ncols() << endl
         << "grad_Ed: " << grad_Ed.nrows() << " * " << grad_Ed.ncols() << endl
         << "grad_Ev: " << grad_Ev.nrows() << " * " << grad_Ev.ncols() << endl
         << "grad_Ec: " << grad_Ec.nrows() << " * " << grad_Ec.ncols() << endl;
    */

    return cost;
}

void NNClassifier::compute_cost_function()
{
    /*
    for (int i = 0; i < W1.nrows(); ++i)
        for (int j = 0; j < W1.ncols(); ++j)
            cost.grad_W1[i][j] = 0.0;
    for (int i = 0; i < b1.size(); ++i)
        cost.grad_b1[i] = 0.0;
    for (int i = 0; i < W2.nrows(); ++i)
        for (int j = 0; j < W2.ncols(); ++j)
            cost.grad_W2[i][j] = 0.0;
    for (int i = 0; i < E.nrows(); ++i)
        for (int j = 0; j < E.ncols(); ++j)
            cost.grad_E[i][j] = 0.0;
    */

    if (config.debug)
        cost.dropout_histories.clear();

    /**
     * Randomly sample a subset of instances
     *  as a mini-batch.
     */
    /*
    samples = Util::get_random_subset(
            dataset.samples,
            config.batch_size);
    */
    Util::get_minibatch(
            dataset.samples,
            samples,
            config.batch_size,
            cursor);
    cursor += samples.size();
    if (cursor >= dataset.n)
        cursor = cursor - dataset.n; // equals to cursor % dataset.n

    cerr << "Sample " << samples.size() << " samples for training" << endl;

    // should be smaller than number of CPU cores.
    int num_chunks = config.training_threads;
    vector< vector<Sample> > chunks;
    Util::partition_into_chunks(samples, chunks, num_chunks);

    /*
    for (size_t i = 0; i < chunks.size(); ++i)
        cerr << "Chunk " << i << " = " << chunks[i].size() << endl;
    */

    /**
     * determine the feature IDs which need to be pre-computed
     * for these examples
     *
     * # I think this is problematic in Danqi's code (grad_saved),
     *      since they loss the dropout information.
     *      (ok, she's right)
     */
    // /* debug
    vector<int> feature_ids_to_pre_compute = 
        get_pre_computed_ids(samples);
    pre_compute(feature_ids_to_pre_compute);
    // */

    for (int i = 0; i < grad_saved.nrows(); ++i)
        for (int j = 0; j < grad_saved.ncols(); ++j)
            grad_saved[i][j] = 0.0;

    // cerr << "build thread pool..." << endl;
    ThreadPool pool(num_chunks);
    vector< future<Cost> > results;
    for (int i = 0; i < num_chunks; ++i)
    {
        // cerr << "build " << i << "-th thread" << endl;
        results.emplace_back(pool.enqueue(&NNClassifier::thread_proc, *this, chunks[i], samples.size()));
        // results.emplace_back(pool.enqueue(&NNClassifier::thread_func, *this, chunks[i], samples.size()));
    }
    // cerr << "all threads built" << endl;

    // Merge
    cost.init();
    for (int i = 0; i < num_chunks; ++i)
    {
        if (i == 0)
            cost = results[i].get(); // R-value
        else
            cost.merge(results[i].get(), config.debug);
    }

    // cost = 0.0;
    // int correct = 0;

    // cost /= config.batch_size;
    // cost /= samples.size();
    // accuracy = (double)correct / (double)config.batch_size;
    // accuracy = (double)correct / (double)samples.size();

    // /* debug
    back_prop_saved(cost, feature_ids_to_pre_compute);
    // */

    // cerr << "loss = " << cost.loss << endl;
    // cerr << "accuracy = " << cost.percent_correct << endl;
    add_l2_regularization(cost);
}

void NNClassifier::back_prop_saved(Cost& cost, vector<int> & features_seen)
{
    // #pragma omp parallel for
    for (size_t i = 0; i < features_seen.size(); ++i)
    {
        // cerr << "cost.grad_Eb[0][0]" << cost.grad_Eb[0][0] << endl;

        int map_x = pre_map[features_seen[i]];

        int tok = features_seen[i] / config.num_tokens;
        // int offset = (features_seen[i] % config.num_tokens) * config.embedding_size;
        int pos = features_seen[i] % config.num_tokens;
        int feat_type = config.get_feat_type(pos);
        int offset = config.get_offset(pos);
        int emb_size = config.get_embedding_size(feat_type);

        int E_index = tok;
        assert (feat_type != Config::NONEXIST);
        if (feat_type == Config::DIST_FEAT)
            E_index -= Eb.nrows();
        else if (feat_type == Config::VALENCY_FEAT)
            E_index -= Eb.nrows() + Ed.nrows();
        else if (feat_type == Config::CLUSTER_FEAT)
            E_index -= Eb.nrows() + Ed.nrows() + Ev.nrows();

        for (int j = 0; j < config.hidden_size; ++j)
        {
            double delta = grad_saved[map_x][j];
            if (feat_type == Config::BASIC_FEAT)
                for (int k = 0; k < emb_size; ++k)
                {
                    cost.grad_W1[j][offset + k] += delta * Eb[E_index][k];
                    cost.grad_Eb[E_index][k] += delta * W1[j][offset + k];
                }
            else if (feat_type == Config::DIST_FEAT)
                for (int k = 0; k < emb_size; ++k)
                {
                    cost.grad_W1[j][offset + k] += delta * Ed[E_index][k];
                    cost.grad_Ed[E_index][k] += delta * W1[j][offset + k];
                }
            else if (feat_type == Config::VALENCY_FEAT)
                for (int k = 0; k < emb_size; ++k)
                {
                    cost.grad_W1[j][offset + k] += delta * Ev[E_index][k];
                    cost.grad_Ev[E_index][k] += delta * W1[j][offset + k];
                }
            else if (feat_type == Config::CLUSTER_FEAT)
                for (int k = 0; k < emb_size; ++k)
                {
                    cost.grad_W1[j][offset + k] += delta * Ec[E_index][k];
                    cost.grad_Ec[E_index][k] += delta * W1[j][offset + k];
                }
        }
    }
}

void NNClassifier::add_l2_regularization(Cost& cost)
{
    // cerr << "regularize W1" << endl;
    for (int i = 0; i < W1.nrows(); ++i)
    {
        for (int j = 0; j < W1.ncols(); ++j)
        {
            cost.loss += config.reg_parameter
                        * W1[i][j]
                        * W1[i][j]
                        / 2.0;
            cost.grad_W1[i][j] += config.reg_parameter
                        * W1[i][j];
        }
    }

    // whether regularize the bias term b1?
    // cerr << "regularize b1" << endl;
    for (int i = 0; i < b1.size(); ++i)
    {
        cost.loss += config.reg_parameter * b1[i] * b1[i] / 2.0;
        cost.grad_b1[i] += config.reg_parameter * b1[i];
    }

    // cerr << "regularize W2" << endl;
    for (int i = 0; i < W2.nrows(); ++i)
    {
        for (int j = 0; j < W2.ncols(); ++j)
        {
            cost.loss += config.reg_parameter
                        * W2[i][j]
                        * W2[i][j]
                        / 2.0;
            cost.grad_W2[i][j] += config.reg_parameter * W2[i][j];
        }
    }

    // cerr << "regularize Eb" << endl;
    for (int i = 0; i < Eb.nrows(); ++i)
    {
        for (int j = 0; j < Eb.ncols(); ++j)
        {
            cost.loss += config.reg_parameter
                        * Eb[i][j]
                        * Eb[i][j]
                        / 2.0;
            cost.grad_Eb[i][j] += config.reg_parameter
                        * Eb[i][j];
        }
    }

    // cerr << "regularize Ed" << endl;
    for (int i = 0; i < Ed.nrows(); ++i)
    {
        for (int j = 0; j < Ed.ncols(); ++j)
        {
            cost.loss += config.reg_parameter
                        * Ed[i][j]
                        * Ed[i][j]
                        / 2.0;
            cost.grad_Ed[i][j] += config.reg_parameter
                        * Ed[i][j];
        }
    }
    // cerr << "regularize Ev" << endl;
    for (int i = 0; i < Ev.nrows(); ++i)
    {
        for (int j = 0; j < Ev.ncols(); ++j)
        {
            cost.loss += config.reg_parameter
                        * Ev[i][j]
                        * Ev[i][j]
                        / 2.0;
            cost.grad_Ev[i][j] += config.reg_parameter
                        * Ev[i][j];
        }
    }
    // cerr << "regularize Ec" << endl;
    for (int i = 0; i < Ec.nrows(); ++i)
    {
        for (int j = 0; j < Ec.ncols(); ++j)
        {
            cost.loss += config.reg_parameter
                        * Ec[i][j]
                        * Ec[i][j]
                        / 2.0;
            cost.grad_Ec[i][j] += config.reg_parameter
                        * Ec[i][j];
        }
    }
    // cerr << "regularize Wv" << endl;
    for (int i = 0; i < Wv.nrows(); ++i)
    {
        for (int j = 0; j < Wv.ncols(); ++j)
        {
            cost.loss += config.reg_parameter
                        * Wv[i][j]
                        * Wv[i][j]
                        / 2.0;
            cost.grad_Wv[i][j] += config.reg_parameter
                                 * Wv[i][j];
        }
    }
    // cerr << "regularize Wr" << endl;
    for (int i = 0; i < Wr.dim1(); ++i)
    {
        for (int j = 0; j < Wr.dim2(); ++j)
        {
            for (int l = 0; l < Wr.dim3(); ++l)
            {
                cost.loss += config.reg_parameter
                            * Wr[i][j][l]
                            * Wr[i][j][l]
                            / 2.0;
                cost.grad_Wr[i][j][l] += config.reg_parameter
                                        * Wr[i][j][l];
            }
        }
    }

}

void NNClassifier::dropout(int size, double prob, vector<int>& active_units)
{
    active_units.clear();
    for (int i = 0; i < size; ++i)
    {
        // if (rand() % 10 / 10 > prob)
        if (Util::rand_double() > prob)
            active_units.push_back(i);
    }
}

void NNClassifier::check_gradient()
{
    /**
     * check gradients computed by @compute_cost_function
     * with numerical gradients
     *
     * @grad_W2
     * @grad_W1
     * @grad_E
     * @grad_b1
     *
     */
    init_gradient_histories();
    cerr << "Checking Gradients..." << endl;
    // first step: randomly sample a mini-batch
    compute_cost_function(); // set cost and gradient

    Mat<double> num_grad_W1(0.0, cost.grad_W1.nrows(), cost.grad_W1.ncols());
    Mat<double> num_grad_W2(0.0, cost.grad_W2.nrows(), cost.grad_W2.ncols());
    Vec<double> num_grad_b1(0.0, cost.grad_b1.size());
    Mat<double> num_grad_Eb(0.0, cost.grad_Eb.nrows(), cost.grad_Eb.ncols());
    Mat<double> num_grad_Ed(0.0, cost.grad_Ed.nrows(), cost.grad_Ed.ncols());
    Mat<double> num_grad_Ev(0.0, cost.grad_Ev.nrows(), cost.grad_Ev.ncols());
    Mat<double> num_grad_Ec(0.0, cost.grad_Ec.nrows(), cost.grad_Ec.ncols());
    Mat<double> num_grad_Wv(0.0, cost.grad_Wv.nrows(), cost.grad_Wv.ncols());
    Mat3<double> num_grad_Wr(0.0, cost.grad_Wr.dim1(), cost.grad_Wr.dim2(), cost.grad_Wr.dim3());

    // second step: compute numerical gradients
    compute_numerical_gradients(
            num_grad_W1,
            num_grad_b1,
            num_grad_W2,
            num_grad_Eb,
            num_grad_Ed,
            num_grad_Ev,
            num_grad_Ec,
            num_grad_Wv,
            num_grad_Wr);

    // second step: compute the diff between two gradients
    // norm(numgrad-grad) / norm(numgrad+grad) should be small
    /*
    cerr << Util::l2_norm(num_grad_W1) << endl;
    cerr << Util::l2_norm(cost.grad_W1) << endl;
    double numerator = Util::l2_norm(Util::mat_subtract(num_grad_W1, cost.grad_W1));
    double denominator = Util::l2_norm(Util::mat_add(num_grad_W1, cost.grad_W1));
    cerr << numerator << "/" << denominator << endl;
    double diff_grad_W1 = numerator / denominator;
    */
    double diff_grad_W1 = Util::l2_norm(Util::mat_subtract(num_grad_W1, cost.grad_W1)) / Util::l2_norm(Util::mat_add(num_grad_W1, cost.grad_W1));
    double diff_grad_b1 = Util::l2_norm(Util::vec_subtract(num_grad_b1, cost.grad_b1)) / Util::l2_norm(Util::vec_add(num_grad_b1, cost.grad_b1));
    double diff_grad_W2 = Util::l2_norm(Util::mat_subtract(num_grad_W2, cost.grad_W2)) / Util::l2_norm(Util::mat_add(num_grad_W2, cost.grad_W2));
    double diff_grad_Eb = Util::l2_norm(Util::mat_subtract(num_grad_Eb, cost.grad_Eb)) / Util::l2_norm(Util::mat_add(num_grad_Eb, cost.grad_Eb));
    double diff_grad_Ed = Util::l2_norm(Util::mat_subtract(num_grad_Ed, cost.grad_Ed)) / Util::l2_norm(Util::mat_add(num_grad_Ed, cost.grad_Ed));
    double diff_grad_Ev = Util::l2_norm(Util::mat_subtract(num_grad_Ev, cost.grad_Ev)) / Util::l2_norm(Util::mat_add(num_grad_Ev, cost.grad_Ev));
    double diff_grad_Ec = Util::l2_norm(Util::mat_subtract(num_grad_Ec, cost.grad_Ec)) / Util::l2_norm(Util::mat_add(num_grad_Ec, cost.grad_Ec));
    double diff_grad_Wv = Util::l2_norm(Util::mat_subtract(num_grad_Wv, cost.grad_Wv)) / Util::l2_norm(Util::mat_add(num_grad_Wv, cost.grad_Wv));
    double diff_grad_Wr = Util::l2_norm(Util::mat3_subtract(num_grad_Wr, cost.grad_Wr)) / Util::l2_norm(Util::mat3_add(num_grad_Wr, cost.grad_Wr));

    /*
    for (int i = 0; i < num_grad_W2.nrows(); ++i)
    {
        for (int j = 0; j < num_grad_W2.ncols(); ++j)
            cerr << num_grad_W2[i][j] << " ";
        cerr << endl;
    }
    */

    cerr << "diff(W1) = " << diff_grad_W1 << endl;
    cerr << "diff(b1) = " << diff_grad_b1 << endl;
    cerr << "diff(W2) = " << diff_grad_W2 << endl;
    cerr << "diff(Eb) = " << diff_grad_Eb << endl;
    cerr << "diff(Ed) = " << diff_grad_Ed << endl;
    cerr << "diff(Ev) = " << diff_grad_Ev << endl;
    cerr << "diff(Ec) = " << diff_grad_Ec << endl;
    cerr << "diff(Wv) = " << diff_grad_Wv << endl;
    cerr << "diff(Wr) = " << diff_grad_Wr << endl;
}

void NNClassifier::compute_numerical_gradients(
        Mat<double> & num_grad_W1,
        Vec<double> & num_grad_b1,
        Mat<double> & num_grad_W2,
        Mat<double> & num_grad_Eb,
        Mat<double> & num_grad_Ed,
        Mat<double> & num_grad_Ev,
        Mat<double> & num_grad_Ec,
        Mat<double> & num_grad_Wv,
        Mat3<double> & num_grad_Wr)
{
    if (samples.size() == 0)
    {
        cerr << "Run compute_cost_function first." << endl;
        return ;
    }

    double epsilon = 1e-6;
    cerr << "checking W1..." << endl;
    // cerr << num_grad_W1.nrows() << ", " << num_grad_W1.ncols() << endl;
    cerr << W1.nrows() << ", " << W1.ncols() << endl;
    for (int i = 0; i < W1.nrows(); ++i)
        for (int j = 0; j < W1.ncols(); ++j)
        {
            W1[i][j] += epsilon;
            double p_eps_cost = compute_cost();
            W1[i][j] -= 2 * epsilon;
            double n_eps_cost = compute_cost();
            num_grad_W1[i][j] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
            W1[i][j] += epsilon; // reset
        }

    cerr << "checking b1..." << endl;
    for (int i = 0; i < b1.size(); ++i)
    {
        b1[i] += epsilon;
        double p_eps_cost = compute_cost();
        b1[i] -= 2 * epsilon;
        double n_eps_cost = compute_cost();
        num_grad_b1[i] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
        b1[i] += epsilon;
    }

    cerr << "checking W2..." << endl;
    for (int i = 0; i < W2.nrows(); ++i)
        for (int j = 0; j < W2.ncols(); ++j)
        {
            W2[i][j] += epsilon;
            double p_eps_cost = compute_cost();
            W2[i][j] -= 2 * epsilon;
            double n_eps_cost = compute_cost();
            num_grad_W2[i][j] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
            W2[i][j] += epsilon; // reset
        }

    cerr << "checking Eb..." << endl;
    for (int i = 0; i < Eb.nrows(); ++i)
        for (int j = 0; j < Eb.ncols(); ++j)
        {
            Eb[i][j] += epsilon;
            double p_eps_cost = compute_cost();
            Eb[i][j] -= 2 * epsilon;
            double n_eps_cost = compute_cost();
            num_grad_Eb[i][j] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
            Eb[i][j] += epsilon; // reset
        }

    cerr << "checking Ed..." << endl;
    for (int i = 0; i < Ed.nrows(); ++i)
        for (int j = 0; j < Ed.ncols(); ++j)
        {
            Ed[i][j] += epsilon;
            double p_eps_cost = compute_cost();
            Ed[i][j] -= 2 * epsilon;
            double n_eps_cost = compute_cost();
            num_grad_Ed[i][j] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
            Ed[i][j] += epsilon; // reset
        }

    cerr << "checking Ev..." << endl;
    for (int i = 0; i < Ev.nrows(); ++i)
        for (int j = 0; j < Ev.ncols(); ++j)
        {
            Ev[i][j] += epsilon;
            double p_eps_cost = compute_cost();
            Ev[i][j] -= 2 * epsilon;
            double n_eps_cost = compute_cost();
            num_grad_Ev[i][j] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
            Ev[i][j] += epsilon; // reset
        }

    cerr << "checking Ec..." << endl;
    for (int i = 0; i < Ec.nrows(); ++i)
        for (int j = 0; j < Ec.ncols(); ++j)
        {
            Ec[i][j] += epsilon;
            double p_eps_cost = compute_cost();
            Ec[i][j] -= 2 * epsilon;
            double n_eps_cost = compute_cost();
            num_grad_Ec[i][j] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
            Ec[i][j] += epsilon; // reset
        }

    cerr << "checking Wv..." << endl;
    for (int i = 0; i < Wv.nrows(); ++i)
        for (int j = 0; j < Wv.ncols(); ++j)
        {
            Wv[i][j] += epsilon;
            double p_eps_cost = compute_cost();
            Wv[i][j] -= 2 * epsilon;
            double n_eps_cost = compute_cost();
            num_grad_Wv[i][j] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
            Wv[i][j] += epsilon; // reset
        }

    cerr << "checking Wr..." << endl;
    for (int i = 0; i < Wr.dim1(); ++i)
        for (int j = 0; j < Wr.dim2(); ++j)
            for (int l = 0; l < Wr.dim3(); ++l)
            {
                Wr[i][j][l] += epsilon;
                double p_eps_cost = compute_cost();
                Wr[i][j][l] -= 2 * epsilon;
                double n_eps_cost = compute_cost();
                num_grad_Wr[i][j][l] = (p_eps_cost - n_eps_cost) / (2 * epsilon);
                Wr[i][j][l] += epsilon; // reset
            }
}

// for gradient checking. Single threading
double NNClassifier::compute_cost()
{
    // make use of samples / dropout_histories

    double v_cost = 0.0;

    int Eb_label_start = Eb.nrows() - Wr.dim1(); // important

    // cerr << "samples.size=" << samples.size() << endl;
    // cerr << "dropout_history.size=" << cost.dropout_histories.size() << endl;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        vector<int> features = samples[i].get_feature();
        vector<int> label = samples[i].get_label();

        vector<int> active_units = cost.dropout_histories[i];
        Vec<double> scores(0.0, num_labels);
        Vec<double> hidden(0.0, config.hidden_size);
        Vec<double> hidden3(0.0, config.hidden_size);

        // feed-forward to hidden layer
        int offset = 0;
        for (int j = 0; j < config.num_tokens; ++j)
        {
            int tok = features[j];
            int E_index = tok;
            int feat_type = config.get_feat_type(j);

            assert (feat_type != Config::NONEXIST);
            if (feat_type == Config::DIST_FEAT)
                E_index -= Eb.nrows();
            else if (feat_type == Config::VALENCY_FEAT)
                E_index -= Eb.nrows() + Ed.nrows();
            else if (feat_type == Config::CLUSTER_FEAT)
                E_index -= Eb.nrows() + Ed.nrows() + Ev.nrows();

            int emb_size = config.get_embedding_size(feat_type);
            // embedding size for current token

            for (size_t k = 0; k < active_units.size(); ++k)
            {
                int node_index = active_units[k];
                if (feat_type == Config::BASIC_FEAT)
                {
                    for (int l = 0; l < emb_size; ++l)
                        hidden[node_index] +=
                            W1[node_index][offset+l] * Eb[E_index][l];
                }
                else if (feat_type == Config::DIST_FEAT)
                {
                    for (int l = 0; l < emb_size; ++l)
                        hidden[node_index] +=
                            W1[node_index][offset+l] * Ed[E_index][l];
                }
                else if (feat_type == Config::VALENCY_FEAT)
                {
                    for (int l = 0; l < emb_size; ++l)
                        hidden[node_index] +=
                            W1[node_index][offset+l] * Ev[E_index][l];
                }
                else if (feat_type == Config::CLUSTER_FEAT)
                {
                    for (int l = 0; l < emb_size; ++l)
                        hidden[node_index] +=
                            W1[node_index][offset+l] * Ec[E_index][l];
                }
            }
            offset += emb_size;
        }

        Mat<double> llc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> lc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> rrc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> rc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);

        Mat<double> lc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> rc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
        Mat<double> root_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);

        // feed forward the composed tokens to hidden layer
        for (int j = 0; j < config.num_compose_tokens; ++j)
        {
            // cerr << "Feed-forward the composition layer " << j << endl;
            // j = 0 | 1
            const unordered_map<string, int> tree = compose_structure[j];

            int tok_w_root = features[tree.find("w_root")->second]; // -> idx_w_root
            int tok_w_lc1  = features[tree.find("w_lc1")->second];
            int tok_w_lc2  = features[tree.find("w_lc2")->second];
            int tok_w_rc1  = features[tree.find("w_rc1")->second];
            int tok_w_rc2  = features[tree.find("w_rc2")->second];
            int tok_w_llc1 = features[tree.find("w_lc1lc1")->second];
            int tok_w_rrc1 = features[tree.find("w_rc1rc1")->second];

            int tok_l_lc1  = features[tree.find("l_lc1")->second];
            int tok_l_lc2  = features[tree.find("l_lc2")->second];
            int tok_l_rc1  = features[tree.find("l_rc1")->second];
            int tok_l_rc2  = features[tree.find("l_rc2")->second];
            int tok_l_llc1 = features[tree.find("l_lc1lc1")->second];
            int tok_l_rrc1 = features[tree.find("l_rc1rc1")->second];

            int idx_l_lc1  = tok_l_lc1 - Eb_label_start;
            int idx_l_lc2  = tok_l_lc2 - Eb_label_start;
            int idx_l_rc1  = tok_l_rc1 - Eb_label_start;
            int idx_l_rc2  = tok_l_rc2 - Eb_label_start;
            int idx_l_llc1 = tok_l_llc1 - Eb_label_start;
            int idx_l_rrc1 = tok_l_rrc1 - Eb_label_start;

            // propagate leaf nodes (embedding) to hidden layers

            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                for (int l = 0; l < Eb.ncols(); ++l)
                {
                    llc1_hidden[j][k] += Wv[k][l] * Eb[tok_w_llc1][l];
                    rrc1_hidden[j][k] += Wv[k][l] * Eb[tok_w_rrc1][l];

                    lc2_hidden[j][k]  += Wv[k][l] * Eb[tok_w_lc2][l];
                    rc2_hidden[j][k]  += Wv[k][l] * Eb[tok_w_rc2][l];

                    lc1_hidden[j][k]  += Wv[k][l] * Eb[tok_w_lc1][l];
                    rc1_hidden[j][k]  += Wv[k][l] * Eb[tok_w_rc1][l];

                    root_hidden[j][k] += Wv[k][l] * Eb[tok_w_root][l];
                }
            }
            // non-linear activation for llc1, rrc1, lc2, rc2
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                llc1_hidden[j][k] = tanh(llc1_hidden[j][k]);
                rrc1_hidden[j][k] = tanh(rrc1_hidden[j][k]);
                lc2_hidden[j][k] = tanh(lc2_hidden[j][k]);
                rc2_hidden[j][k] = tanh(rc2_hidden[j][k]);
            }
            // bias

            // compose llc1+lc1->lc1_hidden
            // compose rrc1+rc1->rc1_hidden
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    lc1_hidden[j][k] += Wr[idx_l_llc1][k][l] * llc1_hidden[j][l];
                    rc1_hidden[j][k] += Wr[idx_l_rrc1][k][l] * rrc1_hidden[j][l];
                }
            }
            // non-linear activation for lc1, lc2
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                lc1_hidden[j][k] = tanh(lc1_hidden[j][k]);
                rc1_hidden[j][k] = tanh(rc1_hidden[j][k]);
            }

            // propagate lc1+lc2+rc2+rc1 -> w_root
            for (int k = 0; k < config.compose_embedding_size; ++k)
            {
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    root_hidden[j][k] += Wr[idx_l_lc1][k][l] * lc1_hidden[j][l] // lc1
                                    + Wr[idx_l_lc2][k][l] * lc2_hidden[j][l] // lc2
                                    + Wr[idx_l_rc2][k][l] * rc2_hidden[j][l] // rc2
                                    + Wr[idx_l_rc1][k][l] * rc1_hidden[j][l];// rc1
                }
            }
            // non-linear activation for w_root
            for (int k = 0; k < config.compose_embedding_size; ++k)
                root_hidden[j][k] = tanh(root_hidden[j][k]);
            // bias

            // propagate the composed root -> hidden
            for (size_t k = 0; k < active_units.size(); ++k)
            {
                int node_index = active_units[k];
                for (int l = 0; l < config.compose_embedding_size; ++l)
                {
                    hidden[node_index] +=
                        W1[node_index][offset+l] * root_hidden[j][l];
                }
            }
            offset += config.compose_embedding_size;
        }

        for (size_t j = 0; j < active_units.size(); ++j)
        {
            int node_index = active_units[j];
            hidden[node_index] += b1[node_index];
            hidden3[node_index] = pow(hidden[node_index], 3);
        }

        // feed forward to softmax layer
        int opt_label = -1;
        for (int j = 0; j < num_labels; ++j)
        {
            for (size_t k = 0; k < active_units.size(); ++k)
            {
                int node_index = active_units[k];
                scores[j] += W2[j][node_index] * hidden3[node_index];
            }
            if (opt_label < 0 || scores[j] > scores[opt_label])
                opt_label = j;
        }

        double sum1 = .0;
        double sum2 = .0;
        double max_score = scores[opt_label];
        for (int j = 0; j < num_labels; ++j)
        {
            if (label[j] >= 0)
            {
                scores[j] = fasterexp(scores[j] - max_score);
                // scores[j] = exp(scores[j] - max_score);
                // scores[j] = exp(scores[j]);
                if (label[j] == 1) sum1 += scores[j];
                sum2 += scores[j];
            }
        }

        v_cost += (log(sum2) - log(sum1));
    }

    v_cost /= samples.size();

    for (int i = 0; i < W1.nrows(); ++i)
    {
        for (int j = 0; j < W1.ncols(); ++j)
        {
            v_cost += config.reg_parameter
                        * W1[i][j]
                        * W1[i][j]
                        / 2.0;
        }
    }

    for (int i = 0; i < b1.size(); ++i)
    {
        v_cost += config.reg_parameter * b1[i] * b1[i] / 2.0;
    }

    for (int i = 0; i < W2.nrows(); ++i)
    {
        for (int j = 0; j < W2.ncols(); ++j)
        {
            v_cost += config.reg_parameter
                        * W2[i][j]
                        * W2[i][j]
                        / 2.0;
        }
    }

    for (int i = 0; i < Ed.nrows(); ++i)
    {
        for (int j = 0; j < Ed.ncols(); ++j)
        {
            v_cost += config.reg_parameter
                        * Ed[i][j]
                        * Ed[i][j]
                        / 2.0;
        }
    }
    for (int i = 0; i < Ev.nrows(); ++i)
    {
        for (int j = 0; j < Ev.ncols(); ++j)
        {
            v_cost += config.reg_parameter
                        * Ev[i][j]
                        * Ev[i][j]
                        / 2.0;
        }
    }
    for (int i = 0; i < Ec.nrows(); ++i)
    {
        for (int j = 0; j < Ec.ncols(); ++j)
        {
            v_cost += config.reg_parameter
                        * Ec[i][j]
                        * Ec[i][j]
                        / 2.0;
        }
    }
    for (int i = 0; i < Wv.nrows(); ++i)
    {
        for (int j = 0; j < Wv.ncols(); ++j)
        {
            v_cost += config.reg_parameter
                        * Wv[i][j]
                        * Wv[i][j]
                        / 2.0;
        }
    }
    for (int i = 0; i < Wr.dim1(); ++i)
    {
        for (int j = 0; j < Wr.dim2(); ++j)
        {
            for (int l = 0; l < Wr.dim3(); ++l)
            {
                v_cost += config.reg_parameter
                            * Wr[i][j][l]
                            * Wr[i][j][l]
                            / 2.0;
            }
        }
    }

    return v_cost;
}

void NNClassifier::take_ada_gradient_step(int E_start_pos)
{
    for (int i = 0; i < W1.nrows(); ++i)
    {
        for (int j = 0; j < W1.ncols(); ++j)
        {
            eg2W1[i][j] += cost.grad_W1[i][j] * cost.grad_W1[i][j];
            W1[i][j] -= config.ada_alpha * cost.grad_W1[i][j] /
                    sqrt(eg2W1[i][j] + config.ada_eps);
        }
    }

    for (int i = 0; i < b1.size(); ++i)
    {
        eg2b1[i] += cost.grad_b1[i] * cost.grad_b1[i];
        b1[i] -= config.ada_alpha * cost.grad_b1[i] /
                    sqrt(eg2b1[i] + config.ada_eps);
    }

    for (int i = 0; i < W2.nrows(); ++i)
    {
        for (int j = 0; j < W2.ncols(); ++j)
        {
            eg2W2[i][j] += cost.grad_W2[i][j] * cost.grad_W2[i][j];
            W2[i][j] -= config.ada_alpha * cost.grad_W2[i][j] /
                    sqrt(eg2W2[i][j] + config.ada_eps);
        }
    }

    for (int i = 0; i < Eb.nrows(); ++i)
    {
        if (config.fix_word_embeddings && i < E_start_pos)
            continue;
        for (int j = 0; j < Eb.ncols(); ++j)
        {
            eg2Eb[i][j] += cost.grad_Eb[i][j] * cost.grad_Eb[i][j];
            Eb[i][j] -= config.ada_alpha * cost.grad_Eb[i][j] /
                    sqrt(eg2Eb[i][j] + config.ada_eps);
        }
    }

    for (int i = 0; i < Ed.nrows(); ++i)
    {
        for (int j = 0; j < Ed.ncols(); ++j)
        {
            eg2Ed[i][j] += cost.grad_Ed[i][j] * cost.grad_Ed[i][j];
            Ed[i][j] -= config.ada_alpha * cost.grad_Ed[i][j] /
                    sqrt(eg2Ed[i][j] + config.ada_eps);
        }
    }

    for (int i = 0; i < Ev.nrows(); ++i)
    {
        for (int j = 0; j < Ev.ncols(); ++j)
        {
            eg2Ev[i][j] += cost.grad_Ev[i][j] * cost.grad_Ev[i][j];
            Ev[i][j] -= config.ada_alpha * cost.grad_Ev[i][j] /
                    sqrt(eg2Ev[i][j] + config.ada_eps);
        }
    }

    for (int i = 0; i < Ec.nrows(); ++i)
    {
        for (int j = 0; j < Ec.ncols(); ++j)
        {
            eg2Ec[i][j] += cost.grad_Ec[i][j] * cost.grad_Ec[i][j];
            Ec[i][j] -= config.ada_alpha * cost.grad_Ec[i][j] /
                    sqrt(eg2Ec[i][j] + config.ada_eps);
        }
    }

    for (int i = 0; i < Wv.nrows(); ++i)
    {
        for (int j = 0; j < Wv.ncols(); ++j)
        {
            eg2Wv[i][j] += cost.grad_Wv[i][j] * cost.grad_Wv[i][j];
            Wv[i][j] -= config.ada_alpha * cost.grad_Wv[i][j] /
                    sqrt(eg2Wv[i][j] + config.ada_eps);
        }
    }

    for (int i = 0; i < Wr.dim1(); ++i)
    {
        for (int j = 0; j < Wr.dim2(); ++j)
        {
            for (int l = 0; l < Wr.dim3(); ++l)
            {
                eg2Wr[i][j][l] += cost.grad_Wr[i][j][l] * cost.grad_Wr[i][j][l];
                Wr[i][j][l] -= config.ada_alpha * cost.grad_Wr[i][j][l] /
                        sqrt(eg2Wr[i][j][l] + config.ada_eps);
            }
        }
    }
}

vector<int> NNClassifier::get_pre_computed_ids(
        vector<Sample>& samples)
{
    set<int> feature_ids;

    for (size_t i = 0; i < samples.size(); ++i)
    {
        vector<int> feats = samples[i].get_feature();
        assert(feats.size() == (unsigned int)config.num_tokens);
        for (size_t j = 0; j < feats.size(); ++j)
        {
            int tok = feats[j];
            int index = tok * config.num_tokens + j;
            if (pre_map.find(index) != pre_map.end())
                feature_ids.insert(index);
        }
    }

    double percent_pre_computed =
        feature_ids.size() / (float)pre_map.size();
    cerr << "Percent necessary to pre-compute: "
         << percent_pre_computed * 100
         << "%"
         << endl;

    return vector<int>(feature_ids.begin(), feature_ids.end());
}

double NNClassifier::get_loss()
{
    return cost.loss;
}

double NNClassifier::get_accuracy()
{
    return cost.percent_correct;
}

Mat<double>& NNClassifier::get_W1()
{
    return W1;
}

Mat<double>& NNClassifier::get_W2()
{
    return W2;
}

Mat<double>& NNClassifier::get_Eb()
{
    return Eb;
}

Mat<double>& NNClassifier::get_Ed()
{
    return Ed;
}

Mat<double>& NNClassifier::get_Ev()
{
    return Ev;
}

Mat<double>& NNClassifier::get_Ec()
{
    return Ec;
}

Vec<double>& NNClassifier::get_b1()
{
    return b1;
}

Mat<double>& NNClassifier::get_Wv()
{
    return Wv;
}

Mat3<double>& NNClassifier::get_Wr()
{
    return Wr;
}

void NNClassifier::pre_compute()
{
    // TODO
    vector<int> candidates;
    // unordered_map<int, int>::iterator iter = pre_map.begin();
    auto iter = pre_map.begin();
    for (; iter != pre_map.end(); ++iter)
        candidates.push_back(iter->first);
    pre_compute(candidates);
}

void NNClassifier::pre_compute(
        vector<int>& candidates,
        bool refill)
{
    cerr << "pre_map.size = " << pre_map.size() << endl;
    cerr << "candidates.size = " << candidates.size() << endl;
    if (refill)
        for (size_t i = 0; i < candidates.size(); ++i)
            pre_map[candidates[i]] = i;

    // re-initialize
    saved.resize(pre_map.size(), config.hidden_size);
    for (int i = 0; i < saved.nrows(); ++i)
        for (int j = 0; j < saved.ncols(); ++j)
            saved[i][j] = 0.0;

    // #pragma omp parallel for
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        int map_x = pre_map[candidates[i]];
        int tok = candidates[i] / config.num_tokens;
        int pos = candidates[i] % config.num_tokens;
        int feat_type = config.get_feat_type(pos);
        int offset = config.get_offset(pos);
        int emb_size = config.get_embedding_size(feat_type);

        // cerr << "\ri = " << i << ": " << candidates[i] << ". map_x = " << map_x << ". tok = " << tok << ". pos = " << pos;

        int E_index = tok;
        assert (feat_type != Config::NONEXIST);
        if (feat_type == Config::DIST_FEAT)
            E_index -= Eb.nrows();
        else if (feat_type == Config::VALENCY_FEAT)
            E_index -= Eb.nrows() + Ed.nrows();
        else if (feat_type == Config::CLUSTER_FEAT)
            E_index -= Eb.nrows() + Ed.nrows() + Ev.nrows();

        // cerr << ". E_index = " << E_index;

        for (int j = 0; j < config.hidden_size; ++j)
        {
            if (feat_type == Config::BASIC_FEAT)
                for (int k = 0; k < emb_size; ++k)
                    saved[map_x][j] += Eb[E_index][k] * W1[j][offset + k];
            else if (feat_type == Config::DIST_FEAT)
                for (int k = 0; k < emb_size; ++k)
                    saved[map_x][j] += Ed[E_index][k] * W1[j][offset + k];
            else if (feat_type == Config::VALENCY_FEAT)
                for (int k = 0; k < emb_size; ++k)
                    saved[map_x][j] += Ev[E_index][k] * W1[j][offset + k];
            else if (feat_type == Config::CLUSTER_FEAT)
                for (int k = 0; k < emb_size; ++k)
                    saved[map_x][j] += Ec[E_index][k] * W1[j][offset + k];
        }
    }

    cerr << "Pre-computed "
         << candidates.size()
         << endl;
}

void NNClassifier::compute_scores(
        vector<int>& features,
        vector<double>& scores)
{
    scores.clear();
    scores.resize(num_labels, 0.0);

    int Eb_label_start = Eb.nrows() - Wr.dim1(); // important

    Vec<double> hidden(0.0, config.hidden_size);
    int offset = 0;
    for (size_t i = 0; i < features.size(); ++i)
    {
        int tok = features[i];
        int E_index = tok;
        int index = tok * config.num_tokens + i;

        int feat_type = config.get_feat_type(i);
        int emb_size = config.get_embedding_size(feat_type);

        assert (feat_type != Config::NONEXIST);
        if (feat_type == Config::DIST_FEAT)
            E_index -= Eb.nrows();
        else if (feat_type == Config::VALENCY_FEAT)
            E_index -= Eb.nrows() + Ed.nrows();
        else if (feat_type == Config::CLUSTER_FEAT)
            E_index -= Eb.nrows() + Ed.nrows() + Ev.nrows();

        if (pre_map.find(index) != pre_map.end())
        {
            int id = pre_map[index];
            for (int j = 0; j < config.hidden_size; ++j)
                hidden[j] += saved[id][j];
        }
        else
        {
            for (int j = 0; j < config.hidden_size; ++j)
            {
                if (feat_type == Config::BASIC_FEAT)
                    for (int k = 0; k < emb_size; ++k)
                        hidden[j] += Eb[E_index][k] * W1[j][offset + k];
                else if (feat_type == Config::DIST_FEAT)
                    for (int k = 0; k < emb_size; ++k)
                        hidden[j] += Ed[E_index][k] * W1[j][offset + k];
                else if (feat_type == Config::VALENCY_FEAT)
                    for (int k = 0; k < emb_size; ++k)
                        hidden[j] += Ev[E_index][k] * W1[j][offset + k];
                else if (feat_type == Config::CLUSTER_FEAT)
                    for (int k = 0; k < emb_size; ++k)
                        hidden[j] += Ec[E_index][k] * W1[j][offset + k];
            }
        }
        offset += emb_size;
    }

    Mat<double> llc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
    Mat<double> lc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
    Mat<double> rrc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
    Mat<double> rc2_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);

    Mat<double> lc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
    Mat<double> rc1_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);
    Mat<double> root_hidden(0.0, config.num_compose_tokens, config.compose_embedding_size);

    // feed forward the composed tokens to hidden layer
    for (int j = 0; j < config.num_compose_tokens; ++j)
    {
        // j = 0 | 1
        const unordered_map<string, int> tree = compose_structure[j];

        int tok_w_root = features[tree.find("w_root")->second]; // -> idx_w_root
        int tok_w_lc1  = features[tree.find("w_lc1")->second];
        int tok_w_lc2  = features[tree.find("w_lc2")->second];
        int tok_w_rc1  = features[tree.find("w_rc1")->second];
        int tok_w_rc2  = features[tree.find("w_rc2")->second];
        int tok_w_llc1 = features[tree.find("w_lc1lc1")->second];
        int tok_w_rrc1 = features[tree.find("w_rc1rc1")->second];

        int tok_l_lc1  = features[tree.find("l_lc1")->second];
        int tok_l_lc2  = features[tree.find("l_lc2")->second];
        int tok_l_rc1  = features[tree.find("l_rc1")->second];
        int tok_l_rc2  = features[tree.find("l_rc2")->second];
        int tok_l_llc1 = features[tree.find("l_lc1lc1")->second];
        int tok_l_rrc1 = features[tree.find("l_rc1rc1")->second];

        int idx_l_lc1  = tok_l_lc1 - Eb_label_start;
        int idx_l_lc2  = tok_l_lc2 - Eb_label_start;
        int idx_l_rc1  = tok_l_rc1 - Eb_label_start;
        int idx_l_rc2  = tok_l_rc2 - Eb_label_start;
        int idx_l_llc1 = tok_l_llc1 - Eb_label_start;
        int idx_l_rrc1 = tok_l_rrc1 - Eb_label_start;

        // propagate leaf nodes (embedding) to hidden layers

        // in contrast to the cube activation function
        //  for sigmoidal functions, we don't have to store
        //  all hidden outputs before activation (W * x + b)
        //  So, we can just overwrite the hidden layer to store
        //  the activated output.
        for (int k = 0; k < config.compose_embedding_size; ++k)
        {
            for (int l = 0; l < Eb.ncols(); ++l)
            {
                llc1_hidden[j][k] += Wv[k][l] * Eb[tok_w_llc1][l];
                rrc1_hidden[j][k] += Wv[k][l] * Eb[tok_w_rrc1][l];

                lc2_hidden[j][k]  += Wv[k][l] * Eb[tok_w_lc2][l];
                rc2_hidden[j][k]  += Wv[k][l] * Eb[tok_w_rc2][l];

                lc1_hidden[j][k]  += Wv[k][l] * Eb[tok_w_lc1][l];
                rc1_hidden[j][k]  += Wv[k][l] * Eb[tok_w_rc1][l];

                root_hidden[j][k] += Wv[k][l] * Eb[tok_w_root][l];
            }
        }
        // non-linear activation for llc1, rrc1, lc2, rc2
        for (int k = 0; k < config.compose_embedding_size; ++k)
        {
            llc1_hidden[j][k] = tanh(llc1_hidden[j][k]);
            rrc1_hidden[j][k] = tanh(rrc1_hidden[j][k]);
            lc2_hidden[j][k] = tanh(lc2_hidden[j][k]);
            rc2_hidden[j][k] = tanh(rc2_hidden[j][k]);
        }
        // bias

        // compose llc1+lc1->lc1_hidden
        // compose rrc1+rc1->rc1_hidden
        for (int k = 0; k < config.compose_embedding_size; ++k)
        {
            for (int l = 0; l < config.compose_embedding_size; ++l)
            {
                lc1_hidden[j][k] += Wr[idx_l_llc1][k][l] * llc1_hidden[j][l];
                rc1_hidden[j][k] += Wr[idx_l_rrc1][k][l] * rrc1_hidden[j][l];
            }
        }
        // non-linear activation for lc1, lc2
        for (int k = 0; k < config.compose_embedding_size; ++k)
        {
            lc1_hidden[j][k] = tanh(lc1_hidden[j][k]);
            rc1_hidden[j][k] = tanh(rc1_hidden[j][k]);
        }

        // propagate lc1+lc2+rc2+rc1 -> w_root
        for (int k = 0; k < config.compose_embedding_size; ++k)
        {
            for (int l = 0; l < config.compose_embedding_size; ++l)
            {
                root_hidden[j][k] += Wr[idx_l_lc1][k][l] * lc1_hidden[j][l] // lc1
                                + Wr[idx_l_lc2][k][l] * lc2_hidden[j][l] // lc2
                                + Wr[idx_l_rc2][k][l] * rc2_hidden[j][l] // rc2
                                + Wr[idx_l_rc1][k][l] * rc1_hidden[j][l];// rc1
            }
        }
        // non-linear activation for w_root
        for (int k = 0; k < config.compose_embedding_size; ++k)
            root_hidden[j][k] = tanh(root_hidden[j][k]);
        // bias

        // propagate the composed root -> hidden
        for (int k = 0; k < config.hidden_size; ++k)
        {
            for (int l = 0; l < config.compose_embedding_size; ++l)
            {
                hidden[k] += W1[k][offset+l] * root_hidden[j][l];
            }
        }
        offset += config.compose_embedding_size;
    }

    for (int i = 0; i < config.hidden_size; ++i)
    {
        hidden[i] += b1[i];
        hidden[i] = hidden[i] * hidden[i] * hidden[i];
    }

    for (int i = 0; i < num_labels; ++i)
        for (int j = 0; j < config.hidden_size; ++j)
            // no need to calculate exp
            scores[i] += W2[i][j] * hidden[j];
}

void NNClassifier::clear_gradient_histories()
{
    init_gradient_histories();
}

void NNClassifier::init_gradient_histories()
{
    eg2W1.resize(W1.nrows(), W1.ncols()); eg2W1 = .0;
    eg2W2.resize(W2.nrows(), W2.ncols()); eg2W2 = .0;
    eg2Eb.resize(Eb.nrows(), Eb.ncols()); eg2Eb = .0;
    eg2Ed.resize(Ed.nrows(), Ed.ncols()); eg2Ed = .0;
    eg2Ev.resize(Ev.nrows(), Ev.ncols()); eg2Ev = .0;
    eg2Ec.resize(Ec.nrows(), Ec.ncols()); eg2Ec = .0;
    eg2b1.resize(b1.size()); eg2b1 = .0;
    eg2Wv.resize(Wv.nrows(), Wv.ncols()); eg2Wv = .0;
    eg2Wr.resize(Wr.dim1(), Wr.dim2(), Wr.dim3()); eg2Wr = .0;
}

void NNClassifier::finalize_training()
{
    // reset
}

void Cost::merge(const Cost & c, bool & debug)
{
    loss += c.loss;
    percent_correct += c.percent_correct;
    Util::mat_inc(grad_W1, c.grad_W1);
    Util::vec_inc(grad_b1, c.grad_b1);
    Util::mat_inc(grad_W2, c.grad_W2);
    Util::mat_inc(grad_Eb, c.grad_Eb);
    Util::mat_inc(grad_Ed, c.grad_Ed);
    Util::mat_inc(grad_Ev, c.grad_Ev);
    Util::mat_inc(grad_Ec, c.grad_Ec);
    Util::mat_inc(grad_Wv, c.grad_Wv);
    Util::mat3_inc(grad_Wr, c.grad_Wr);

    if (debug)
        dropout_histories.insert(
                dropout_histories.end(),
                c.dropout_histories.begin(),
                c.dropout_histories.end());
}

void NNClassifier::print_info()
{
    cerr << "\tW1: " << W1.nrows() << " * " << W1.ncols() << endl
         << "\tW2: " << W2.nrows() << " * " << W2.ncols() << endl
         << "\tb1: " << b1.size()  << endl
         << "\tEb: " << Eb.nrows() << " * " << Eb.ncols() << endl
         << "\tEd: " << Ed.nrows() << " * " << Ed.ncols() << endl
         << "\tEv: " << Ev.nrows() << " * " << Ev.ncols() << endl
         << "\tEc: " << Ec.nrows() << " * " << Ec.ncols() << endl
         << "\tWv: " << Wv.nrows() << " * " << Wv.ncols() << endl
         << "\tWr: " << Wr.dim1() << " * " << Wr.dim2() << " * " << Wr.dim3() << endl;
}

