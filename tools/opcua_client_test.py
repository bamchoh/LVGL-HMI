#!/usr/bin/env python3
"""Small OPC UA client for the HML runtime's opcua_server driver (needs: pip install asyncua).

  python tools/opcua_client_test.py                          # list every tag under Objects/HML
  python tools/opcua_client_test.py --write speed_sp=77      # write a tag (type is taken from the node)
  python tools/opcua_client_test.py --url opc.tcp://raspberrypi:4840 --ns urn:hml:runtime --folder HML
"""
import argparse
import asyncio

from asyncua import Client, ua


def coerce(text: str, vtype: ua.VariantType):
    if vtype == ua.VariantType.Boolean:
        return text.strip().lower() in ("1", "true", "on")
    if vtype in (ua.VariantType.Int16, ua.VariantType.Int32, ua.VariantType.Int64,
                 ua.VariantType.UInt16, ua.VariantType.UInt32, ua.VariantType.UInt64):
        return int(float(text))
    if vtype in (ua.VariantType.Float, ua.VariantType.Double):
        return float(text)
    return text


async def main(args):
    async with Client(url=args.url) as client:
        ns = await client.get_namespace_index(args.ns)
        folder = client.get_node(ua.NodeId(args.folder, ns))
        print(f"connected to {args.url}, namespace index {ns}")
        for child in await folder.get_children():
            name = (await child.read_browse_name()).Name
            dv = await child.read_data_value()
            print(f"  {name:<12} = {dv.Value.Value!r:<24} ({dv.Value.VariantType.name})")

        for spec in args.write or []:
            tag, _, text = spec.partition("=")
            node = client.get_node(ua.NodeId(tag, ns))
            vtype = (await node.read_data_value()).Value.VariantType
            await node.write_value(ua.DataValue(ua.Variant(coerce(text, vtype), vtype)))
            print(f"wrote {tag} = {await node.read_value()!r}")


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--url", default="opc.tcp://127.0.0.1:4840")
    p.add_argument("--ns", default="urn:hml:runtime")
    p.add_argument("--folder", default="HML")
    p.add_argument("--write", action="append", metavar="TAG=VALUE")
    asyncio.run(main(p.parse_args()))
