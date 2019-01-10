#!/bin/bash

wrk -t32 -c1000 -d60s --latency http://localhost
