#!/bin/bash/

nohup sh ./scripts/run.sh clnndep > log/fix/`date +%Y-%m-%d-%H%M%S`.fix.sdp.log &
nohup sh ./scripts/run.cdt.sh clnndep > log/fix/`date +%Y-%m-%d-%H%M%S`.fix.cdt.log &
nohup sh ./scripts/run.ctb7.sh clnndep > log/fix/`date +%Y-%m-%d-%H%M%S`.fix.ctb7.log &
