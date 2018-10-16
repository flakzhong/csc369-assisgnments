def convert(filename):
    output = open("analyzed_" + filename, "w+")
    instruction = dict()
    data = dict()

    with open(filename) as file:
        for curr_line in file:
            curr_line = curr_line.split(",")
            addr = curr_line[0][:-3]
            if curr_line[1] == "I" or curr_line[1] == "I\n":
                if addr in instruction:
                    value = instruction[addr]
                    instruction.update({addr:value + 1})
                else:
                    instruction.update({addr:1})
            else:
                if addr in data:
                    value = data[addr]
                    data.update({addr:value + 1})
                else:
                    data.update({addr:1})

    output.write("Instructions:\n")
    for addr, count in instruction.items():
        output.write(addr + "," + str(count) + "\n")
    output.write("\n")
    output.write("Data:\n")
    for addr, count in data.items():
        output.write(addr + "," + str(count) + "\n")
    file.close()
    output.close()


if __name__ == "__main__":
    convert("tr-heaploop.ref")
    convert("tr-matmul.ref")
