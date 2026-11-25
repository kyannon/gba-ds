import sys; import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen3 import *
PARTY=[
 dict(species=3,level=40,name="VENUSAUR",hp=120,maxhp=132,personality=0x1A2B3C41,moves=(22,75,73,79),pp=(25,25,10,15),stats=(85,87,91,100,100)),
 dict(species=17,level=30,name="PIDGEOTTO",hp=18,maxhp=78,personality=0x0F0E0D12,moves=(98,16,17,28),pp=(30,35,35,15),stats=(60,55,70,50,50)),
 dict(species=25,level=28,name="PIKACHU",hp=66,maxhp=66,personality=0x22446688,moves=(84,98,86,97),pp=(30,30,20,30),status=0x40,stats=(52,40,70,50,50)),
 dict(species=74,level=24,name="GEODUDE",hp=0,maxhp=62,personality=0x13572468,moves=(33,111,88,120),pp=(35,40,15,5),stats=(60,70,25,30,30)),
 dict(species=143,level=35,name="SNORLAX",hp=150,maxhp=180,personality=0x70000007,moves=(34,133,29,156),pp=(15,15,15,10),status=0x08,stats=(100,80,30,60,100)),
 dict(species=130,level=33,name="GYARADOS",hp=99,maxhp=110,personality=0x31415926,moves=(55,44,37,82),pp=(25,25,20,10),stats=(95,80,90,60,80)),
]
def party_bytes(n=6):
    return b"".join(build_mon(**m) for m in PARTY[:n]).ljust(600,b"\0")
if __name__=="__main__":
    print(party_bytes().hex())
