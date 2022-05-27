#!/bin/bash
../simulator 1 fairness_exp/conf_fairness.txt > fairness_exp/output_fairness.txt &
wait
echo "Done."
