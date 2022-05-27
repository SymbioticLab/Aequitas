#!/bin/bash
../simulator 1 slo_compliance_3qos_exp/conf_baseline.txt > slo_compliance_3qos_exp/output_slo_comp_3qos_baseline.txt &
../simulator 1 slo_compliance_3qos_exp/conf_aequitas.txt > slo_compliance_3qos_exp/output_slo_comp_3qos_aequitas.txt &
wait
echo "Done."
