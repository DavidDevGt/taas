import socket
import time
import struct
import statistics
import gc
import array

HOST = '127.0.0.1'
PORT = 1588
COUNT = 10000 

latencies = array.array('d', [0.0] * COUNT)
hw_timestamps = array.array('Q', [0] * COUNT)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.connect((HOST, PORT))
sock.settimeout(1.0)

get_time = time.perf_counter
send = sock.send
recv = sock.recv
unpack = struct.unpack

print(f"[*] Initialize...")

gc.collect()
gc.disable()

try:
    for i in range(COUNT):
        t0 = get_time()
        
        send(b'T')
        data = recv(8)
        
        t1 = get_time()
        
        latencies[i] = (t1 - t0) * 1_000_000

        hw_timestamps[i] = unpack('<Q', data)[0]

finally:
    gc.enable()

min_lat = min(latencies)
max_lat = max(latencies)
avg_lat = statistics.mean(latencies)
stdev = statistics.stdev(latencies)
jitter_pico = max_lat - min_lat

print("\n" + "="*40)
print("RESULTADOS")
print("="*40)
print(f"Paquetes: {COUNT}")
print(f"Latencia Mínima : {min_lat:.2f} us")
print(f"Latencia Promedio: {avg_lat:.2f} us")
print(f"Latencia Máxima : {max_lat:.2f} us")
print(f"Desviación Est.: {stdev:.2f} us")
print("-" * 40)
print(f"JITTER REAL: {jitter_pico:.2f} us")
print("="*40)
print(f"Último HW TS: {hw_timestamps[-1]}")
