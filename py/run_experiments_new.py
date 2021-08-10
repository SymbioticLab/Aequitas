#!/usr/bin/python

import subprocess
import threading
import multiprocessing
import os

conf_veritas = '''init_cwnd: 2
max_cwnd: 30
retx_timeout: 450
queue_size: 524288
propagation_delay: 0.0000002
bandwidth: 100000000000.0
host_type: 1
queue_type: 6
flow_type: 6
num_flow: {0}
num_hosts: {1}
flow_trace: ./CDF_{2}.txt
burst_size: {3}
qos_ratio: {4}
hardcoded_targets: {5}
targets: {6}
cut_through: 0
pfabric_priority_type: size
mean_flow_size: 0
load_balancing: 0
preemptive_queue: 0
debug_event_info: 0
enable_flow_lookup: 0
flow_lookup_id: 1
priority_downgrade: 1
dp_alpha: 0.01
dp_beta: 0.01
channel_multiplexing: 1
multiplex_constant: 10
use_burst_byte: 1
burst_with_no_spacing: 0
real_nic: 0
load_measure_interval: 10
test_size_to_priority: 0
smart_time_window: 1
target_pctl: 1000
memory_time_duration: 5000
high_prio_lat_target: 10
target_expiration: 2000000
qd_expiration: 50
rtt_expiration: 300000
downgrade_batch_size: 20
expiration_count: 250
mtu: 5120
normalized_lat: 1
big_switch: 1
num_agg_switches: 16
num_core_switches: 9
traffic_imbalance: 0
traffic_pattern: 1
disable_veritas_cc: 0
disable_pkt_logging: 1
disable_cwnd_logging: 1
disable_dwnd_logging: 1
disable_poisson_arrival: 0
test_fairness: 0
track_qosh_dwnds: 0
load: 0.8
use_dynamic_load: 1
burst_load: 1.4
num_pctl: 10
use_random_jitter: 1
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
qos_weights: 8,4,1
'''

conf_pfabric = '''init_cwnd: 30
max_cwnd: 30
retx_timeout: 450
queue_size: 524288
propagation_delay: 0.0000002
bandwidth: 100000000000.0
queue_type: 2
flow_type: 2
num_flow: {0}
num_hosts: {1}
flow_trace: ./CDF_{2}.txt
burst_size: {3}
qos_ratio: {4}
hardcoded_targets: {5}
targets: {6}
cut_through: 0
pfabric_priority_type: size
pfabric_limited_priority: {7}
mean_flow_size: 0
load_balancing: 0
preemptive_queue: 0
debug_event_info: 0
enable_flow_lookup: 0
flow_lookup_id: 0
priority_downgrade: 0
dp_alpha: 0.01
dp_beta: 0.01
channel_multiplexing: 1
multiplex_constant: 1
use_burst_byte: 1
burst_with_no_spacing: 0
load_measure_interval: 10
smart_time_window: 1
target_pctl: 1000
memory_time_duration: 5000
high_prio_lat_target: 10
target_expiration: 2000000
qd_expiration: 50
rtt_expiration: 300000
downgrade_batch_size: 20
expiration_count: 250
mtu: 5120
normalized_lat: 1
big_switch: 1
num_agg_switches: 16
num_core_switches: 9
host_type: 1
traffic_imbalance: 0
traffic_pattern: 1
disable_veritas_cc: 0
disable_pkt_logging: 1
disable_cwnd_logging: 1
disable_dwnd_logging: 1
disable_poisson_arrival: 0
test_fairness: 0
track_qosh_dwnds: 0
load: 0.8
use_dynamic_load: 1
burst_load: 1.4
num_pctl: 10
use_random_jitter: 1
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
qos_weights: 8,4,1
'''

conf_qjump = '''init_cwnd: 2
max_cwnd: 30
retx_timeout: 450
queue_size: 524288
propagation_delay: 0.0000002
bandwidth: 100000000000.0
host_type: 7
queue_type: 7
flow_type: 7
num_flow: {0}
num_hosts: {1}
flow_trace: ./CDF_{2}.txt
burst_size: {3}
qos_ratio: {4}
hardcoded_targets: {5}
targets: {6}
qjump_tput_factor: 1,16,32
cut_through: 0
pfabric_priority_type: size
enable_qjump_retransmission: 1
mean_flow_size: 0
load_balancing: 0
preemptive_queue: 0
debug_event_info: 0
enable_flow_lookup: 0
flow_lookup_id: 1
priority_downgrade: 0
dp_alpha: 0.01
dp_beta: 0.01
channel_multiplexing: 1
multiplex_constant: 1
use_burst_byte: 1
burst_with_no_spacing: 0
real_nic: 0
load_measure_interval: 10
test_size_to_priority: 0
smart_time_window: 1
target_pctl: 1000
memory_time_duration: 5000
high_prio_lat_target: 10
target_expiration: 2000000
qd_expiration: 50
rtt_expiration: 300000
downgrade_batch_size: 20
expiration_count: 250
mtu: 5120
normalized_lat: 1
big_switch: 1
num_agg_switches: 16
num_core_switches: 9
traffic_imbalance: 0
traffic_pattern: 1
disable_veritas_cc: 0
disable_pkt_logging: 1
disable_cwnd_logging: 1
disable_dwnd_logging: 1
disable_poisson_arrival: 0
test_fairness: 0
track_qosh_dwnds: 0
load: 0.8
use_dynamic_load: 1
burst_load: 1.4
num_pctl: 10
use_random_jitter: 1
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
qos_weights: 8,4,1
'''


qos_ratio = ['60,30,10']
#runs = ['veritas']
runs = ['pfabric_ideal', 'pfabric_real']
#runs = ['qjump']
#burst_size = [4]
burst_size = [131072, 262144, 393216]
traffic_size = [32]
## create the "./config" and "./result" by yourself :(
binary = '../simulator'
template = binary + ' 1 ./exp_config/conf_{0}{1}_{2}_D{3}_B{4}_FT{5}.txt > ./result/result_{0}{1}_{2}_D{3}_B{4}_FT{5}.txt'
#cdf_RPC = ['uniform_32K']
cdf_RPC = ['read_req_latency']
targets = [('10,15', '300,450')]    # tuple[0]: hardcoded_targets; tuple[1]: targets (final)


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
                    for t in targets:
                        num_flow = 5000000
                        #  generate conf file
                        if r == 'veritas':
                            conf_str = conf_veritas.format(num_flow, N + 1, cdf, burst, ratio, t[0], t[1])
                        elif r == 'pfabric_ideal':
                            conf_str = conf_pfabric.format(num_flow, N + 1, cdf, burst, ratio, t[0], t[1], '0')
                        elif r == 'pfabric_real':
                            conf_str = conf_pfabric.format(num_flow, N + 1, cdf, burst, ratio, t[0], t[1], '1')
                        elif r == 'qjump':
                            conf_str = conf_qjump.format(num_flow, N + 1, cdf, burst, ratio, t[0], t[1])
                        else:
                            assert False, r

                        # Note modify the config dir name
                        confFile = "./exp_config/conf_{0}{1}_{2}_D{3}_B{4}_FT{5}.txt".format(r, N, cdf, ratio.replace(',', '_'), burst, t[1].replace(',', '_'))
                        with open(confFile, 'w') as f:
                            f.write(conf_str)

                        threads.append(threading.Thread(target=run_exp, args=((r, N, cdf, ratio.replace(',', '_'), burst, t[1].replace(',', '_')), semaphore)))

print '\n'
[t.start() for t in threads]
[t.join() for t in threads]
print 'finished', len(threads), 'experiments'
