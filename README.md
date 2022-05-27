# Aequitas
This project contains the simulator code used in Aequitas.

The simulator is based on [YAPS](https://github.com/NetSys/simulator).

For more details, please refer to our SIGCOMM' 22 paper. <!-- TODO: add paper link after camera-ready -->

For SIGCOMM' 22 artifact evaluation, please refer to the <strong>artifact-eval</strong> branch.

# Build
```
git clone https://github.com/SymbioticLab/Aequitas.git
cd Aequitas
aclocal
autoconf
automake --add-missing
./configure
make
```

# Configuration
Before launching the simulator, you may want to configure a few parameters in the configuration file located in Aequitas/py.
- <strong>flow_trace</strong>: CDF of RPC sizes used in the experiment
- <strong>load</strong>: average load
- <strong>burst_load</strong>: load during burst (should be > 1)
- <strong>traffic_pattern</strong>: incast or all-to-all
- <strong>burst_size</strong>: number of RPCs sent within the burst period
- <strong>qos_weights</strong>: QoS weights starting from the highest QoS, separated by comma. (e.g., 8,4,1) This also implies the number of QoS classes.
- <strong>qos_ratio</strong>: QoS distribution starting from the highest QoS, separated by comma and should sum up to 100. (e.g., 60,30,10)
- <strong>hardcoded_targets</strong>: QoS RPC network latency target starting from the highest QoS (excluding the lowest one) (e.g., 15,25)
- <strong>priority_downgrade</strong>: whether priority downgrade can happen. Must be 1 to have Aequitas admission control.
- <strong>normalized_lat</strong>: whether latency targets are normalized based on MTU; should be 1 for MTU-based SLOs with mixed-sized RPCs
- <strong>flow_type, queue_type, host_type</strong>: used to specify different work, including Aequitas and others. See ext/factory.h for more details.

For information on other parameters, please refer to the code or contact the author.

# Run
```
cd py
../simulator 1 [config_file]
```


# Reference
Please consider citing our paper if you find Justitia related to your research project.
```bibtex
@inproceedings{aequitas-sigcomm22,
  title={Aequitas: Admission Control for Performance-Critical RPCs in Datacenters},
  author={Yiwen Zhang and Gautam Kumar and Nandita Dukkipati and Xian Wu and Priyaranjan Jha and Mosharaf Chowdhury and Amin Vahdat},
  booktitle={SIGCOMM},
  year={2022}
}
```

# Contact
Yiwen Zhang (yiwenzhg@umich.edu)
