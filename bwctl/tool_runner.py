from bwctl.utils import BwctlProcess
from bwctl.tools import get_tool
from bwctl.tools import get_tool

class ToolRunner(BwctlProcess):
    def __init__(self, test=None, results_cb=None):
        self.test = test
        self.results_cb = results_cb

        tool = get_tool(test.tool)

        cmd_line = tool.build_command_line(test)

        start_time = None
        end_time   = None

        if test.local_client:
            start_time = test.scheduling_parameters.reservation_start_time
        else:
            start_time = test.scheduling_parameters.test_start_time

        end_time = test.scheduling_parameters.reservation_end_time

        self.cmd_runner = CmdRunner(start_time=start_time, end_time=end_time, cmd_line=cmd_line)

    def run(self):
        test_results = None

        try:
            cmd_results = self.cmd_runner.run_cmd()
            test_results = self.test.tool_obj.get_results(exit_status=cmd_results.return_code,
                                                          stdout=cmd_results.stdout,
                                                          stderr=cmd_results.stderr)
        except Exception as e:
            err = SystemProblemException(str(e))
            test_results = Results(status="failed", bwctl_errors=[ err ])

        self.results_cb(test_results)

        return results
