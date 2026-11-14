import vltest_bootstrap
import vhdl_frontend

test.scenarios('vlt_all')
test.top_filename = 't/t_vhdl_enum_index.vhdl'

test.compile(make_top_shell=False,
             make_main=False,
             verilator_flags2=[
                 *vhdl_frontend.flags(), '--language', 'vhdl', '--exe', test.pli_filename])

test.execute()

test.passes()
