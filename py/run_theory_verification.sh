#!/bin/bash
../simulator 1 theory_verification_exp/theory_conf_30_70.txt > theory_verification_exp/output_theory_30_70.txt &
../simulator 1 theory_verification_exp/theory_conf_80_20.txt > theory_verification_exp/output_theory_80_20.txt &
wait
echo "Done."
