#!/bin/bash/

nohup sh ./scripts/run.sh clnndep > log/`date +%Y-%m-%d-%H%M%S`.finetune.sdp.gradient_check.log &
#nohup sh ./scripts/run.cdt.sh clnndep > log/fix/`date +%Y-%m-%d-%H%M%S`.fix.cdt.norm.log &
#nohup sh ./scripts/run.ctb7.sh clnndep > log/fix/`date +%Y-%m-%d-%H%M%S`.fix.ctb7.norm.log &
#nohup sh ./scripts/run.webparsing.sh wsj clnndep > log/webparsing/`date +%Y-%m-%d-%H%M%S`.wsj.project.5k.2version.log &
#nohup sh ./scripts/run.cdt.sh clnndep > log/fix/`date +%Y-%m-%d-%H%M%S`.fix.cdt.log &
#nohup sh ./scripts/run.ctb7.sh clnndep > log/fix/`date +%Y-%m-%d-%H%M%S`.fix.ctb7.log &
