"""Probe this OpenShores checkout's local Docker farm without creating an avatar."""
from pathlib import Path
import argparse
import hashlib
import ipaddress
import re

from configure import read_env

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--env-file', type=Path, default=root / 'build/server/local/compose.env',
                    help='Local generated settings file containing the test account')
mode = parser.add_mutually_exclusive_group()
mode.add_argument('--login-only', action='store_true', help='Check credentials without starting a session')
mode.add_argument('--status-only', action='store_true', help='Check scene readiness without authenticating')
args = parser.parse_args()
import socket
import struct

values = read_env(args.env_file)
if not values or values.get('CREATE_LOCAL_PLAYER') != 'true':
    raise SystemExit('Run make server-config with CREATE_LOCAL_PLAYER=true first')
versions = (root / 'src/lib/util/auversion.h').read_text()
def version(name):
    return re.search(r'g_' + name + r'\s*=\s*"([^"]+)"', versions)[1]

def string(value):
    data=value.encode('utf-16-be')
    return struct.pack('>I',len(data))+data
def array(data):
    return struct.pack('>I',len(data))+data
def frame(data):
    size=len(data)
    for extra in range(4):
        if size < (1 << (6+8*extra)):
            return bytes([(extra<<6)|(size>>(8*extra))])+size.to_bytes(4,'big')[4-extra:]+data
    raise ValueError('packet too large')
def receive(sock):
    def exact(count):
        data=b''
        while len(data)<count:
            part=sock.recv(count-len(data))
            if not part:
                raise RuntimeError('Connection closed before packet completed')
            data+=part
        return data
    first=exact(1)[0]
    size=int.from_bytes(bytes([first&63])+exact(first>>6),'big')
    if size > 4*1024*1024:
        raise RuntimeError('Unexpected packet size')
    return exact(size)
def connect(host,port):
    for address in socket.getaddrinfo(host,port,type=socket.SOCK_STREAM):
        if not ipaddress.ip_address(address[4][0]).is_loopback:
            raise RuntimeError('Refusing to send local credentials outside loopback')
    return socket.create_connection((host,port),timeout=30)
def encrypted_name(name):
    key=(0x0101+((ord(version('LoginVersion')[0])<<8)|ord(version('Version')[0])))&0xffff
    seed=(key>>8)&15; inc=key&15
    return bytes(b^((seed+i*inc)&255) for i,b in enumerate(name.encode('latin1')))

login_only = args.login_only
status_only = args.status_only
if not login_only:
    with connect('127.0.0.1',16759) as sock:
        sock.sendall(frame(bytes([79])))
        reply=receive(sock)
        if reply[0] != 79 or len(reply)<21:
            raise RuntimeError('Unexpected scene status response')
        managers,workers,players,cores=struct.unpack('>HHIH',reply[11:21])
        print(f'Scene farm: database status {reply[1]}, managers {managers}, workers {workers}, worker cores {cores}')
        if reply[1] not in (1,2) or managers<1 or workers<1:
            raise RuntimeError('Scene farm is not ready yet')

if status_only:
    raise SystemExit(0)

packet=bytes([14 if login_only else 3])+string(version('LoginVersion'))+array(encrypted_name(values['LOCAL_PLAYER_NAME']))+array(hashlib.sha1(values['LOCAL_PLAYER_PASSWORD'].encode()).digest())
host,port='127.0.0.1',16757
for attempt in range(3):
    with connect(host,port) as sock:
        sock.sendall(frame(packet)); reply=receive(sock)
    if reply[0] == 2:
        size=struct.unpack('>I',reply[1:5])[0]
        host=reply[5:5+size].decode('utf-16-be')
        port=struct.unpack('>H',reply[5+size:7+size])[0]
        print(f'Login redirect: {host}:{port}')
        continue
    if reply[0] != (14 if login_only else 3):
        raise RuntimeError(f'Login rejected: packet {reply[0]}')
    confirm,player=struct.unpack('>II',reply[1:9])
    print(f'Account authentication succeeded; local player ID {player}')
    break
else:
    raise RuntimeError('Too many login redirects')
if login_only:
    raise SystemExit(0)

# DtSceneServerPacket::PL_Login = 73. Use this checkout's scene version;
# no avatar is selected or created by this probe.
with connect('127.0.0.1',16759) as sock:
    login=b'\x49'+string(version('SceneVersion'))+struct.pack('>III',player,confirm,0)+struct.pack('>BHBB',2,0,64,4)
    sock.sendall(frame(login))
    for attempt in range(30):
        reply=receive(sock)
        if reply[0] == 74:
            raise RuntimeError(f'Scene login failed: code {reply[1]}')
        if reply[0] == 73:
            print(f"Scene master accepted version {version('SceneVersion')} and authenticated the local session")
            # Explicitly end this scene connection; graphical testing can now
            # sign in afresh using the same disposable local account.
            sock.sendall(frame(bytes([75])))
            break
    else:
        raise RuntimeError('No scene login acknowledgement received')
with connect(host,port) as sock:
    sock.sendall(frame(bytes([7])+struct.pack('>II',player,confirm)))
    receive(sock)
print('Probe logged out cleanly')
