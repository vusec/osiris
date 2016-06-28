#!/bin/bash

# apache tests
sudo /home/skl/apache/bin/apachectl start
./apache/bin/ab -n 40000 -c 40 "http://127.0.0.1:80/"
sudo /home/skl/apache/bin/apachectl stop

