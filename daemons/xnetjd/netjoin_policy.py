#! /usr/bin/env python
#

import logging
import hashlib
import networkx
import ndap_pb2
import ndap_beacon

class NetjoinPolicy:

    JOINED, JOINING, UNJOINABLE = range(3)

    def __init__(self):
        logging.debug("Policy module initialized")

        # Beacons seen before. One set for each policy instance
        self.known_beacons = {}

        #TODO Load list of available auth providers from config file
        #TODO Load list of XIP networks we can join?

    def print_known_beacons(self):
        print self.known_beacons.keys()

    def keep_known_beacon_id(self, beacon_ID, state):
        if beacon_ID in self.known_beacons:
            logging.debug("Overwriting beacon ID: %s" % beacon_ID)
        self.known_beacons[beacon_ID] = state

    def remove_known_beacon_id(self, beacon_ID):
        if beacon_ID in self.known_beacons:
            del self.known_beacons[beacon_ID]
        else:
            logging.error("Removing non-existing beacon: %s" % beacon_ID)

    # Determine if the beacon has a network descriptor we can deal with
    def sane_beacon(self, beacon):
        # Should have at least two nodes and 1 edge
        if num(beacon.auth_cap.nodes) < 2:
            return False
        if num(beacon.auth_cap.edges) < 1:
            return False
        valid_l2_types = []
        valid_l2_types.append(ndap_pb2.LayerTwoIdentifier.ETHERNET)
        valid_l2_types.append(ndap_pb2.LayerTwoIdentifier.WIFI)
        valid_l2_types.append(ndap_pb2.LayerTwoIdentifier.DSRC)
        if beacon.l2_id.l2_type not in valid_l2_types:
            return False
        return True

    # Remove nodes that we cannot satisfy from the joining graph
    def reduce_graph(self, G):
        # Load list of auth providers we support
        # Load list of XIP networks we can join
        # Check each node except 'Start' against the two lists
        return G

    # Walk a graph and find all XIP nodes
    def find_xip_nodes(self, G):
        xip_nodes = []
        for node in G.nodes:
            #if type(node) is ndap_pb2.XIP_Node:
            if node.has_xip():
                xip_nodes.append(node)
        return xip_nodes

    # Get a path of nodes we can satisfy to join an XIP network, or None
    def get_shortest_joinable_path(self, beacon):

        # Sanity check the beacon
        if not sane_beacon(beacon):
            logging.error("Bad beacon ignored")
            return False
        # Build a directed graph object we can traverse
        beacon_graph = networkx.DiGraph()

        # List of nodes including a fake Start node
        nodes_in_order = ['Start'] + beacon.auth_cap.nodes
        beacon_graph.add_nodes_from(nodes_in_order)

        # Now add edges by indexing into nodes_in_order
        for edge in beacon.auth_cap.edges:
            beacon_graph.add_edge(nodes_in_order[edge.from_node], nodes_in_order[edge.to_node])

        # Reduce graph - take out nodes we can't deal with
        beacon_graph = reduce_graph(beacon_graph)

        # We will join any available XIP network. Get a list of all XIP nodes
        goal_nodes = find_xip_nodes(beacon_graph)

        # Walk the graph to find a path matching available auth providers
        start = nodes_in_order[0]
        path = None
        for xip_node in goal_nodes:
            try:
                path = networkx.shortest_path(beacon_graph, start, xip_node)
                logging.debug("Can join: {}".format(path))
                #TODO: find paths to all goal_nodes, try all if first fails
                break
            except networkx.NetworkXNoPath, e:
                pass
        return path

    # Get a beacon ID from serialized protobuf containing beacon
    def get_serialized_beacon_id(serialized_beacon):
        return hashlib.sha256(serialized_beacon).hexdigest()

    # Check given beacon ID against list of known beacons
    def is_known_beacon_id(self, beacon_id):
        if beacon_id in self.known_beacons:
            return True
        return False

    # Main entry point for the policy module
    def process_serialized_beacon(serialized_beacon):

        # Retrieve ID of beacon so we can test against known beacons
        beacon_ID = get_serialized_beacon_id(serialized_beacon)

        # If this beacon has been processed before, drop it
        if is_known_beacon_id(beacon_ID):
            return

        # Convert beacon to an object we can look into
        beacon = ndap_pb2.NetDescriptor()
        beacon.ParseFromString(serialized_beacon)
        logging.info("Beacon: {}".format(ndap_beacon.beacon_str(beacon)))

        # Try to find a set of steps we can take to join a desirable network
        path = get_shortest_joinable_path(beacon)
        joining_state = None
        if path:
            # Initiate Netjoiner action and add to known_beacons as JOINING
            logging.debug("Joining: {}".format(ndap_beacon.beacon_str(beacon)))
            joining_state = self.JOINING
        else:
            # Add to known_beacons as UNJOINABLE
            joining_state = self.UNJOINABLE

        # We processed this beacon, so record our decision
        keep_known_beacon_id(beacon_ID, joining_state)

# Unit test this module when run by itself
if __name__ == "__main__":
    policy = NetjoinPolicy()
    serialized_test_beacon = ndap_beacon.build_beacon()
    test_beacon = ndap_pb2.NetDescriptor()
    test_beacon.ParseFromString(serialized_test_beacon)
    beacon_ID = policy.get_serialized_beacon_id(serialized_test_beacon)
    policy.keep_known_beacon_id(beacon_ID, NetjoinPolicy.JOINING)
    policy.print_known_beacons()
    policy.remove_known_beacon_id(beacon_ID)
    policy.print_known_beacons()