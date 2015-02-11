#!/bin/bash

cd /export/a04/jguo/work/parser/clnndep

# lang=${lang}
corpus=udt/en/

f_train=$corpus/en-universal-train.conll
f_dev=$corpus/en-universal-dev.conll

f_model=$corpus/model.proj.d100.h400
f_conf=conf/nndep_full.cfg

./nndep -train $f_train \
        -dev $f_dev     \
        -model $f_model \
        -cfg $f_conf \
        -emb resources/projected/en/wmt11-envectors.txt

