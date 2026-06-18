from pyngham import PyNGHamSPP

x = PyNGHamSPP()

pkt = x.encode_tx_pkt(
    0,
    [1,2,3,4,5]
)

print(pkt)

print(x.decode(pkt))


from pyngham import PyNGHamExtension

x = PyNGHamExtension()

pl = []

pl = x.append_id_pkt(
    pl,
    x.encode_callsign("TESTSAT", 1),
    1
)

print(pl)

print(x.decode(pl))