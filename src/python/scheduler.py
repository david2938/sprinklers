import requests
import json

class ApiInvoker:
    def __init__(self, host):
        self.host = host
        self.host_ip = None

    def get_url(self, url):
        full_url = f"http://{self.host_ip or self.host}{url}"
        result = requests.get(full_url)
        status = json.loads(result.text)

        if status["status"] != "ok":
            raise Exception(f"status: {result.text}")

        return result

    def get_status(self):
        result = self.get_url("/status")
        status = json.loads(result.text)

        if status["status"] == "ok":
            self.host_ip = status["addr"]
        else:
            raise Exception(f"status: {result.text}")

        return result.text

    def turn_zone_on(self, zone):
        if self.host_ip is None:
            self.get_status()

        url = f"http://{self.host_ip or self.host}/zone/{zone}/on"
        requests.get(url)

    def turn_zone_off(self, zone):
        if self.host_ip is None:
            self.get_status()

        url = f"http://{self.host_ip or self.host}/zone/{zone}/off"
        requests.get(url)


def main():
    api = ApiInvoker("sprinklers.local")
    api.get_status()
    # api.turn_zone_on(4)

if __name__ == "__main__":
    main()