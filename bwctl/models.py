from bwctl import jsonobject
from bwctl.tools import get_tool

class BWCTLError(jsonobject.JsonObject):
    error_code = jsonobject.IntegerProperty(exclude_if_none=True)
    error_msg  = jsonobject.StringProperty(exclude_if_none=True)

class Results(jsonobject.JsonObject):
    status = jsonobject.StringProperty(exclude_if_none=True)
    results = jsonobject.DictProperty(unicode, exclude_if_none=True)
    bwctl_errors = jsonobject.ListProperty(BWCTLError, exclude_if_none=True)

class ClientSettings(jsonobject.JsonObject):
    address   = jsonobject.StringProperty(exclude_if_none=True)
    protocol  = jsonobject.FloatProperty(exclude_if_none=True)
    time      = jsonobject.DateTimeProperty(exact=True, exclude_if_none=True)
    ntp_error = jsonobject.FloatProperty(exclude_if_none=True)

class SchedulingParameters(jsonobject.JsonObject):
    priority               = jsonobject.FloatProperty(exclude_if_none=True)
    requested_time         = jsonobject.DateTimeProperty(exact=True, exclude_if_none=True)
    latest_acceptable_time = jsonobject.DateTimeProperty(exact=True, exclude_if_none=True)
    accepted_time          = jsonobject.DateTimeProperty(exact=True, exclude_if_none=True)

class Endpoint(jsonobject.JsonObject):
    address   = jsonobject.StringProperty(exclude_if_none=True)
    test_port = jsonobject.IntegerProperty(exclude_if_none=True)

    bwctl_protocol = jsonobject.FloatProperty(exclude_if_none=True)
    peer_port = jsonobject.IntegerProperty(exclude_if_none=True)
    base_path = jsonobject.StringProperty(exclude_if_none=True)
    test_id   = jsonobject.StringProperty(exclude_if_none=True)

    legacy_client_endpoint = jsonobject.BooleanProperty(exclude_if_none=True)
    posts_endpoint_status  = jsonobject.BooleanProperty(exclude_if_none=True)

    client_time_offset = jsonobject.IntegerProperty(exclude_if_none=True, default=0)
    ntp_error = jsonobject.FloatProperty(exclude_if_none=True)

class Test(jsonobject.JsonObject):
    id                    = jsonobject.StringProperty(exclude_if_none=True)

    server_status         = jsonobject.StringProperty(exclude_if_none=True)
    client_status         = jsonobject.StringProperty(exclude_if_none=True)

    client                = jsonobject.ObjectProperty(ClientSettings, exclude_if_none=True)

    # The endpoints of the test
    sender_endpoint       = jsonobject.ObjectProperty(Endpoint, exclude_if_none=True)
    receiver_endpoint     = jsonobject.ObjectProperty(Endpoint, exclude_if_none=True)

    scheduling_parameters = jsonobject.ObjectProperty(SchedulingParameters, exclude_if_none=True)

    # Tool parameters
    tool                  = jsonobject.StringProperty(exclude_if_none=True, required=True)
    tool_parameters       = jsonobject.JsonObject(exclude_if_none=True)

    @property
    def fuzz(self):
        offset = 0
        if self.sender_endpoint:
            offset = offset + self.sender_endpoint.client_time_offset

        if self.sender_endpoint:
            offset = offset - self.sender_endpoint.client_time_offset

        if offset < 0:
            offset = -offset

        return offset

    @property
    def test_type(self):
        tool = get_tool(self.tool)
        if not tool:
            raise Exception("Unknown tool type")

        return tool.type

    @property
    def duration(self):
        tool = get_tool(self.tool)
        if not tool:
            raise Exception("Unknown tool type")

        return tool.duration(self)

    @property
    def bandwidth(self):
        tool = get_tool(self.tool)
        if not tool:
            raise Exception("Unknown tool type")

        return tool.bandwidth(self)
