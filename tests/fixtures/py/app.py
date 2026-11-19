import os
DOC = """
import fake_mod
def leaked():
    pass
"""

def real():
    return os.getenv("HOME")

class T:
    def m(self):
        return 1
