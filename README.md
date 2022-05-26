# Aequitas
This project contains the simulator code used in Aequitas.

The simulator is based on [YAPS](https://github.com/NetSys/simulator).

For more details, please refer to our SIGCOMM' 22 paper. <!-- TODO: add paper link after camera-ready -->

<!-- TODO: mention artifact eval version is in the other branch. -->

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

# Run
```
cd run
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
