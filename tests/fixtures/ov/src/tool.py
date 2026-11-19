import os
from src.helpers import helper

class Tool:
    def execute(self):
        return helper(1)

def go():
    return os.getenv('HOME')
