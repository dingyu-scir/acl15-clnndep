#!/bin/bash
ROOT=`pwd`
SIG=`date '+%Y-%m-%d-%H%M%S'`neural-giga.sdp.gradient_check
WORKSPACE=${ROOT}/workspace/${SIG}

MODEL_DIR=${WORKSPACE}/model
OUTPUT_DIR=${WORKSPACE}/output
EXE_DIR=${WORKSPACE}/bin

mkdir -p ${MODEL_DIR}
mkdir -p ${OUTPUT_DIR}
mkdir -p ${EXE_DIR}

MODEL_PREFIX=${MODEL_DIR}/model

CFG=${EXE_DIR}/nndep.cfg.${SIG}
cp ${ROOT}/conf/debug.cfg  ${CFG}

EXE=${EXE_DIR}/nndep.${SIG}
cp ${ROOT}/bin/$1  ${EXE}

#CORPUS=/users2/yding/branchZuopar/ZuoPar/data/dependency/sem10fold
#F_TRAIN=${CORPUS}/trainr.1.conll
#F_TEST=${CORPUS}/testr.1.conll
#F_OUTPUT=${OUTPUT_DIR}/test.out.sdp
CORPUS=/users2/yding/syndep_zpar/final_data/dependency2/experiment
F_TRAIN=${CORPUS}/trainr.conll
F_DEVEL=${CORPUS}/develr.conll
F_TEST=${CORPUS}/testr.conll
F_OUTPUT=${OUTPUT_DIR}/test.out.sdp

#EMBED=./data/postag/ctb5.pos.added
#EMBED=./data/postag/ctb7.pos.added
#EMBED=./data/giga.ctb7.added2
#EMBED=~/yoavgo-word2vecf-90e299816bcd/giga-r47-output/dim50vecs.bin
#EMBED=/users2/yding/wang2vec-master/giga.50.embed
EMBED=/users2/yding/Word2vector/giga-2.w1.bin
#EMBED=./data/ctb5.pretraining.wangadded
#EMBED=./data/ctb5.pretraining.added
#EMBED=./data/cdt.pretraining.added
#W1=./data/pretrainedW1/W1_ctb5.pretrained
#W1=./data/pretrainedW1/cdt.pretrained.W1
#W1=./data/pretrainedW1/ctb7.pretrained.W1

${EXE}  -train ${F_TRAIN} \
        -dev   ${F_DEVEL} \
        -model ${MODEL_PREFIX} \
        -cfg   ${CFG} \
        -emb   ${EMBED}
#        -W1    ${W1}

echo "test start"

${EXE} -test   ${F_TEST} \
       -model  ${MODEL_PREFIX} \
       -output ${F_OUTPUT} \
       -cfg    ${CFG} \
       -emb    ${EMBED}

perl /users2/yding/branchZuopar/ZuoPar/scripts/dependency/eval.pl -q -g ${F_TEST} -s ${F_OUTPUT}
perl /users2/yding/branchZuopar/ZuoPar/scripts/dependency/eval.pl -q -g ${F_TEST} -s ${F_OUTPUT} -p
