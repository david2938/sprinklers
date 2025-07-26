"""
NAME - sprinkler_url_tester.py

DESCRIPTION

    Stress-tests the board's ability to serve up the UI files successfully
    without failing.  If there are failures, this could be a sign that the
    files have become too large and need to be broken up or minified.

USAGE

    python sprinkler_ui_tester.py [host [repeat]]

OPTIONS

    host - (optional) the Wemos D1 mini to target

        - note, if you want to supply the repeat option, you must supply
          host before it
        - default value: 192.168.7.65 (sptest.local)

    repeat - (optional) the number of times to execute the test

        - default value: 500

NOTES

    - You can also target the production deployment at "sprinklers.local"
      (but it would probably be wise to ping it to get its IP address so
      that you can avoid Bonjour local name resolution, which is very slow
      and might even cause timeouts)

    - Failures also could be caused by bad network connections.  This is very
      difficult to diagnose, but the symptom is that incomplete transfers
      happen, which would reduce the count that succeeded.  Should this start
      to occur, disconnect the board and let it be off-line for at least 30
      seconds so that the Eeros totally let go of its connection.  Then plug
      it back in.  Hopefully it will connect to the closest Eero (which you
      probably can verify in the Eero app).  Once that is done, run the test
      again to see if it improves.  You may even have to repeat this a couple
      of times.
"""

import sys
import urllib.request


def is_response_for_url_200(url: str) -> bool:
    url = f"http://{url}"
    with urllib.request.urlopen(url) as response:
        return response.status == 200


if __name__ == '__main__':
    repeat = 500
    host = "192.168.7.65"

    # len(sys.argv) always equals at least 1 because item 0 is the name
    #  of this script

    match len(sys.argv):
        case 2:
            host = sys.argv[1]
        case 3:
            host = sys.argv[1]
            repeat = int(sys.argv[2])

    files = ["index.html", "sprinklers.js"]

    print(f"Testing against {host}:")

    for f in files:
        print(f"Running {repeat} requests for {f}...", end="")
        sys.stdout.flush()

        responses = [is_response_for_url_200(f"{host}/{f}") for _ in range(repeat)]
        count_okay = len([r for r in responses if r])
        print(f" {count_okay} were okay")
