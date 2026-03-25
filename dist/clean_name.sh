#!/bin/bash

set -e

strip --strip-all release/dashtup
sed -i 's|pietro|xxxxxx|g' release/dashtup