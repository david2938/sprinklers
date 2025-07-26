# Copyright 2025 David Main
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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