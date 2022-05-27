#!/bin/bash
../simulator 1 slo_compliance_2qos_exp/conf_slo_15.txt > slo_compliance_2qos_exp/output_slo_comp_2qos_slo_15.txt &
../simulator 1 slo_compliance_2qos_exp/conf_slo_30.txt > slo_compliance_2qos_exp/output_slo_comp_2qos_slo_30.txt &
../simulator 1 slo_compliance_2qos_exp/conf_slo_45.txt > slo_compliance_2qos_exp/output_slo_comp_2qos_slo_45.txt &
../simulator 1 slo_compliance_2qos_exp/conf_slo_60.txt > slo_compliance_2qos_exp/output_slo_comp_2qos_slo_60.txt &
wait
echo "Done."
