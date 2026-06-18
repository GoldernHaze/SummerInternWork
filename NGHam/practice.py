from pyngham import PyNGHam

x = PyNGHam()

payload = [0, 1, 2, 3, 4]

pkt = x.encode(payload)

print("Encoded Packet:")
print(pkt)

decoded = x.decode(pkt)

print("\nDecoded:")
print(decoded)



from pyngham import PyNGHam

x = PyNGHam()

pkt = x.encode("HELLO")

print("Packet Length:", len(pkt))
print(pkt)
print("   \n ")



from pyngham import PyNGHam

x = PyNGHam()

pkt = x.encode([0,1,2,3,4])

print("Original Decode:")
print(x.decode(pkt))

pkt[30] = 5

print("\nCorrupted Packet Decode:")
print(x.decode(pkt))



print("\n string")

from pyngham import PyNGHam

x = PyNGHam()

pkt = x.encode("Hello CubeSat")

print(pkt)

decoded = x.decode(pkt)

print(decoded)