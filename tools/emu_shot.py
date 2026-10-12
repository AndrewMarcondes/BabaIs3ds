"""Save top and bottom screen of the running Azahar via its RPC server (enable it in Azahar settings)."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
import AzaharRPC

out = sys.argv[1] if len(sys.argv) > 1 else "build-host/emu"
c = AzaharRPC.AzaharRPC(timeout=5, retries=2)
open(out + ".png", "wb").write(c.screenshot())
print("saved", out + ".png")
