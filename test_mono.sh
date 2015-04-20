#!/bin/bash

corpus=ptb/

iter=$1

f_test=$corpus/test.dep
f_model=$corpus/model.senna.d50.h400/model

f_conf=conf/nndep_mono.cfg

./nndep -test $f_test \
        -model $f_model \
        -output $f_test.predict \
        -cfg $f_conf \
        -emb resources/senna.emb
        # -emb resources/wmt11-100.emb


