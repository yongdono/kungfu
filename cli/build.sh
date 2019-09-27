#!/usr/bin/env bash

if [ "$(uname)" == "Darwin" ]; then
    npm run build:mac
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
    npm run build:linux
fi