"""Protobuf Struct wire transport for the routing gateway, without generated stubs."""
import grpc
from google.protobuf.struct_pb2 import Struct
from google.protobuf.json_format import MessageToDict, ParseDict

SERVICE = 'ecoroute.gateway.v1.RoutingGateway'

def serialize(data):
    return ParseDict(data, Struct()).SerializeToString()

def deserialize(data):
    message = Struct()
    message.ParseFromString(data)
    return MessageToDict(message, preserving_proto_field_name=True)

def call(channel, method, payload, timeout=30):
    return channel.unary_unary(f'/{SERVICE}/{method}',request_serializer=serialize,
                               response_deserializer=deserialize)(payload, timeout=timeout)
