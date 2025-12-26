enc = "higvctx jpek"

# Caesar shift -4
shifted = ""
for c in enc:
    if 'a' <= c <= 'z':
        shifted += chr((ord(c) - ord('a') - 4) % 26 + ord('a'))
    elif 'A' <= c <= 'Z':
        shifted += chr((ord(c) - ord('A') - 4) % 26 + ord('A'))
    else:
        shifted += c

print("After Caesar:", shifted)

# # Hex decode
# result = bytes.fromhex(shifted).decode()
# print("Decoded ASCII:", result)
