# Aequitas
This branch is used to faciliate the Artifact Evaluation process for SIGCOMM'22.

For more details and future updates of the simulator, please refer to the master branch.

# Build
```
git clone https://github.com/SymbioticLab/Aequitas.git
git checkout artifact-eval
cd Aequitas
aclocal
autoconf
automake --add-missing
./configure
make
```

# Reproducing Major Results
## Verifying theoretical 2-QoS worst-case delay
The following scirpt launches the experiments to verify the theoretical results of the 2-QoS worst case delay (Figure 10 in the paper).

```
cd py
bash run_theory_verification.sh
```

Here we pick 2 representative QoS-mix (H/L), 30%/70% and 80%/20%.
To calculate the normalized delay (y-axis Figure 10), take the worst case delay in the output log and divide it by the avg sending period reported at the end of the output log.
You will see that both points matches the values in the figure.
Feel free to change the qos_ratio in the config file to test out more data points.

There is a list of commonly used parameters in the master branch.

## SLO compliance (3-QoS)
The following script launches the experiments on the SLO compliance performance using Aequitas vs. baseline in the 3-QoS setting (Figure 12 in the paper).

```
cd py
bash run_slo_compliance_3qos.sh
```

It is one of the major experiments to illustrate the idea of Aequitas. The experiment will take a while to finish. Tail latency results are reported at the end of the output log.

## SLO compliance (2-QoS)
The following script launches the experiments on the SLO compliance performance of Aequitas in the 2-QoS setting (Figure 11 in the paper).

```
cd py
bash run_slo_compliance_2qos.sh
```

The purpose of this set of experiments is to show how Aequitas catches various SLO targets.

## QoS-mix convergence
The following script launches the experiments on the QoS-mix convergence of Aequitas in a 3-QoS setting (Figure 14 in the paper).

```
cd py
bash run_qos_mix_convergence.sh
```

The idea is to show Aequitas can converge back to the same QoS-mix given a set of SLO targets with different input QoS-mix, so watch out for the final converged QoS-mix in the output log.

## Fairness
The following script launches the experiments on the fairness performance of Aequitas (Figure 18 in the paper).

```
cd py
bash run_fairness.sh
```

This experiment is to illustrate Aequitas preserve fairness for the sender that is under its quota by not downgrading its RPCs, so pay attention to the admit probability printed at the end of the output log.

## Notes
Experiments using a production RPC size distribution cannot be done here due to confidentiality. We apologize for the inconvience but this is a company policy.
For realted work comparison, we upload the config files in ```py/related_work_comp``` that we use to perform our related work comparison experiments (Section 6.10 in the paper). Interested readers are welcomed to try out with their favorite RPC size distributions.


# Contact
Yiwen Zhang (yiwenzhg@umich.edu)
