#!/bin/zsh
set -e
cd /Users/audezest/AscendSpire/Tools/ContentWorkbench
if [[ ! -d node_modules ]]; then
  npm install
fi
open http://localhost:3000
exec npm run dev
