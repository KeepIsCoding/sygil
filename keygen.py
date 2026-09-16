def generate_token(alias, state=0):
    h = 0x811c9dc5
    for char in alias:
        if char in ('\n', '\r'):
            break
        h ^= ord(char)
        h = (h * 0x01000193) & 0xFFFFFFFF
    
    if state:
        h ^= 0xDEADBEEF
        h &= 0xFFFFFFFF
        
    p1 = (h ^ 0x5947494C) & 0xFFFFFFFF
    p2 = ((h >> 16) ^ (h & 0xFFFF)) & 0xFFFF
    p3 = (p1 ^ p2 ^ 0x535947) & 0xFFFFFFFF
    
    return f"syg-{p1:08x}-{p2:04x}-{p3:08x}"

if __name__ == "__main__":
    name = input("Enter true name: ").strip()
    if len(name) < 4:
        print("Error: Name must be at least 4 characters.")
    else:
        print("Binding seal:", generate_token(name))
