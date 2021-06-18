#!/usr/bin/python

import subprocess
import threading
import multiprocessing
import os

conf_str_all_to_allN = '''init_cwnd: 2
max_cwnd: 30
retx_timeout: 450
queue_size: 1048576
propagation_delay: 0.0000002
bandwidth: 100000000000.0
queue_type: 6
flow_type: 6
num_flow: {0}
num_hosts: {4}
flow_trace: ./CDF_{1}.txt
cut_through: 0
mean_flow_size: 0
load_balancing: 0
preemptive_queue: 0
num_pctl: 10
big_switch: 1
num_agg_switches: 16
num_core_switches: 9
host_type: 1
traffic_imbalance: 0
traffic_pattern: 1
disable_veritas_cc: 0
disable_pkt_logging: 1
disable_cwnd_logging: 1
load: 0.8
use_dynamic_load: 1
burst_load: 1.2
burst_size: {3}
priority_downgrade: 0
hardcoded_targets: 15,25
high_prio_lat_target: 10
target_expiration: 50000
downgrade_window: 20
expiration_count: 250
rtt_expiration: 0
use_random_jitter: 1
random_flow_start: 1
enable_initial_shift: 0
reauth_limit: 3
magic_trans_slack: 1.1
magic_delay_scheduling: 1
use_flow_trace: 0
smooth_cdf: 0
bytes_mode: 1
burst_at_beginning: 0
capability_timeout: 1.5
capability_resend_timeout: 9
capability_initial: 8
capability_window: 8
capability_window_timeout: 25
ddc: 0
ddc_cpu_ratio: 0.33
ddc_mem_ratio: 0.33
ddc_disk_ratio: 0.34
ddc_normalize: 2
ddc_type: 0
deadline: 0
schedule_by_deadline: 0
avg_deadline: 0.0001
capability_third_level: 1
capability_fourth_level: 0
magic_inflate: 1
interarrival_cdf: none
num_host_types: 13
permutation_tm: 0
flushing_coefficient: 10
early_pkt_in_highest_prio: 0
cc_delay_target: 10
qos_weights: 4,1
qos_ratio: {2}
'''

qos_ratio = ['10,90', '20,80', '30,70', '40,60', '50,50', '60,40', '70,30', '80,20', '90,10', '95,5', '99,1']
#qos_ratio = ['99,1']
#qos_ratio = ['50,50']
#qos_ratio = ['70,20,10']
#qos_ratio = ['10,20,70', '20,20,60', '30,20,50', '40,20,40', '50,20,30', '60,20,20', '70,20,10']
runs = ['all_to_all']
#burst_size = [64,256]
burst_size = [4]
#burst_size = [1,2,4,8,16,32,64,128,256,512]
traffic_size = [32]
## create the "./config" and "./result" by yourself :(
binary = 'coresim/simulator'
template = binary + ' 1 ./exp_config/conf_{0}{1}_{2}_D{3}_B{4}.txt > ./result/result_{0}{1}_{2}_D{3}_B{4}.txt'
cdf_temp = './CDF_{}.txt'
#cdf_RPC = ['uniform_4K', 'uniform_32K']
cdf_RPC = ['uniform_32K']
#cdf_RPC = ['write_req']


def getNumLines(trace):
    out = subprocess.check_output('wc -l {}'.format(trace), shell=True)
    return int(out.split()[0])


def run_exp(str, semaphore):
    semaphore.acquire()
    print template.format(*str)
    subprocess.call(template.format(*str), shell=True)
    semaphore.release()

threads = []
semaphore = threading.Semaphore(multiprocessing.cpu_count())

for r in runs:
    for cdf in cdf_RPC:
        for ratio in qos_ratio:
            for burst in burst_size:
                for N in traffic_size:          # incast size or all-to-all size
                    num_flow = 1000000
                    #  generate conf file
                    if r == 'all_to_all':
                        conf_str = conf_str_all_to_allN.format(num_flow, cdf, ratio, burst, N + 1)
                    else:
                        assert False, r

                    # Note modify the config dir name
                    confFile = "./exp_config/conf_{0}{1}_{2}_D{3}_B{4}.txt".format(r, N, cdf, ratio.replace(',', '_'), burst)
                    with open(confFile, 'w') as f:
                        #print confFile
                        f.write(conf_str)

                    threads.append(threading.Thread(target=run_exp, args=((r, N, cdf, ratio.replace(',', '_'), burst), semaphore)))

print '\n'
[t.start() for t in threads]
[t.join() for t in threads]
print 'finished', len(threads), 'experiments'
