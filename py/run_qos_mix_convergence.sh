#!/bin/bash
../simulator 1 qos_mix_convergence_exp/conf_60_30_10.txt > qos_mix_convergence_exp/output_slo_conv_60_30_10.txt &
../simulator 1 qos_mix_convergence_exp/conf_50_30_20.txt > qos_mix_convergence_exp/output_slo_conv_50_30_20.txt &
../simulator 1 qos_mix_convergence_exp/conf_40_40_20.txt > qos_mix_convergence_exp/output_slo_conv_40_40_20.txt &
../simulator 1 qos_mix_convergence_exp/conf_25_25_50.txt > qos_mix_convergence_exp/output_slo_conv_25_25_50.txt &
wait
echo "Done."
