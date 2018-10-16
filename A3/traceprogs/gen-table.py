import sys

refSumm = dict
    

type_of_rows = {
    "Hit count": int,
    "Miss count": int,
    "Clean evictions" : int,
    "Dirty evictions": int,
    "Hit rate": float,
    "Miss rate": float
}
# everything :: (String) -> refSumm
everything = dict()

current_column = ""

for current_str in sys.stdin:
    current_str = current_str[:-1]
    if(current_str == ""):
        continue
    checkvar = "File name:"
    if(checkvar in current_str):
        iid = current_str.index(checkvar) + len(checkvar) 
        current_column = current_str[iid:]
        everything[current_column] = refSumm()
        continue

    for checkvar in type_of_rows:
        if(checkvar in current_str):
            iid = current_str.index(checkvar) + len(checkvar) + 1
            current_column_value = current_str[iid:]
            #sys.stderr.write(current_str);
            #sys.stderr.write(current_column_value)
            everything[current_column][checkvar] = type_of_rows[checkvar](current_column_value)
    

for dicts in everything.values():
    dicts["Overall eviction count"] = dicts["Clean evictions"] + dicts["Dirty evictions"]

def asKey(e):
    a = "".join(filter(lambda x: x.isdecimal(), e))
    return (int(a), e[:e.index(a)])

    
order = list(everything.keys())
order.sort(key=asKey)
csv_file_str = ""
all_rows = ["Hit rate", "Hit count", "Miss count","Overall eviction count", "Clean evictions", "Dirty evictions"]
for filename in order:
    current_row_strings = []
    for rows in all_rows:
        current_row_strings.append(str(everything[filename][rows]))
    current_row_strings = [filename] + current_row_strings
    current_csv_file_row = ",".join(current_row_strings)
    csv_file_str += current_csv_file_row + '\n'

csv_file_str = ",".join(["Tbl"] + all_rows) + "\n" + csv_file_str
sys.stdout.write(csv_file_str)
