#!/bin/bash
ROOT=`pwd`
SIG=`date '+%Y-%m-%d-%H%M%S'`neural-giga.50.h400.cdt.goldpos.fixortune.normornon-norm
WORKSPACE=${ROOT}/workspace/${SIG}

MODEL_DIR=${WORKSPACE}/model
OUTPUT_DIR=${WORKSPACE}/output
EXE_DIR=${WORKSPACE}/bin

mkdir -p ${MODEL_DIR}
mkdir -p ${OUTPUT_DIR}
mkdir -p ${EXE_DIR}

MODEL_PREFIX=${MODEL_DIR}/model

CFG=${EXE_DIR}/nndep.cfg.${SIG}
cp ${ROOT}/conf/nndep.cfg  ${CFG}

EXE=${EXE_DIR}/nndep.${SIG}
cp ${ROOT}/bin/$1  ${EXE}

CORPUS=/users2/yding/data_consortium
F_TRAIN=${CORPUS}/ldc-train.conll
F_TEST=${CORPUS}/ldc-test.conll
F_DEVEL=${CORPUS}/ldc-holdout.conll
#CORPUS=/users2/yding/data_consortium/ldc
#F_TRAIN=${CORPUS}/ldc-train.autopos.conll
#F_TEST=${CORPUS}/ldc-test.autopos.conll
#F_DEVEL=${CORPUS}/ldc-devel.autopos.conll
F_OUTPUT=${OUTPUT_DIR}/test.out.cdt

#EMBED=./data/giga.ctb7.added2
#EMBED=./data/ctb7auto.gigaw1.added
#EMBED=./data/ctb5.pretrain.auto.added
#EMBED=~/yoavgo-word2vecf-90e299816bcd/giga-r47-output/dim50vecs.bin
#EMBED=/users2/yding/wang2vec-master/giga.50.embed
EMBED=/users2/yding/Word2vector/giga-50.w1.bin
#EMBED=./data/ctb5.pretraining.added
#W1=./data/pretrainedW1/W1_ctb5.pretrained
#W1=./data/pretrainedW1/ctb7.pretrained.W1
#W1=./data/pretrainedW1/ctb7.auto.pretrained.W1
#W1=./data/pretrainedW1/ctb5.auto.pretrained.W1

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
