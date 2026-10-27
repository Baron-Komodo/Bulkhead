import json
import requests

with open("military_vessels.json", "r", encoding="utf-8") as file:
    data = json.load(file)
    l = []
    for i in data["vessels"]:
        # print(i["type"])
        if i["type"] not in l:
            l.append(i["type"])
    print(l)
# print(type(data))
# print("Number of elements:", len(data))


# l = {"country": []}
# url = "https://datasets-server.huggingface.co/rows?dataset=Aniket96%2Fcountry-flags-dataset&config=default&split=train&offset=0&length=100"

# response = requests.get(url, timeout=30)
# response.raise_for_status()

# json_db = response.json()

# for i in json_db["rows"]:
#     print(f"Downloading: {i}")
#     with open(f'data/flag/{i["row"]["label"]}.png', 'wb') as f:
#         l["country"].append({
#             "country": i["row"]["label"],
#             "country_code":i["row"]["country_code"],
#             "image_path": f"data/flag/" + i["row"]["country_code"]  + ".png"
#         })
#         f.write(requests.get(i["row"]["image"]["src"]).content)
#         print(f"doing:" + i["row"]["label"])

# with open("data/flag/flag.json", "w") as f:
#     f.write(str(l))
