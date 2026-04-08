import socket

s = socket.socket()
s.connect(("192.168.10.5", 23))

while True:
    data = s.recv(4096)
    print(len(data))