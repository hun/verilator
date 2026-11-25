-- DESCRIPTION: Verilator: VHDL interface view ports on the top entity
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package axis_pkg is
  type axis_rec_type is record
    ready : std_logic;
    valid : std_logic;
    data  : std_logic_vector(7 downto 0);
  end record axis_rec_type;

  view axis_master_view of axis_rec_type is
    ready : in;
    valid : out;
    data  : out;
  end view axis_master_view;

  alias axis_slave_view is axis_master_view'converse;
end package axis_pkg;

library ieee;
use ieee.std_logic_1164.all;
use work.axis_pkg.all;

entity t is
  port (
    s_axis : view axis_slave_view;
    m_axis : view axis_master_view
  );
end entity t;

architecture rtl of t is
begin
  -- A combinational bridge: the master sees the slave's beat, the slave the master's ready
  m_axis.valid <= s_axis.valid;
  m_axis.data  <= not s_axis.data;
  s_axis.ready <= m_axis.ready;
end architecture rtl;
