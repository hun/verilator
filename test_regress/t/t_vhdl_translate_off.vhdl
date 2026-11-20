-- DESCRIPTION: Verilator: comment pragma translate_off regions are not part of the design
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity t is
  port (
    a  : in  std_logic_vector(3 downto 0);
    o  : out std_logic_vector(3 downto 0);
    o2 : out std_logic_vector(3 downto 0)
  );
end entity t;

architecture rtl of t is
-- synthesis translate_off
  -- Simulation-only: textio is not something a synthesizable model needs
  procedure sim_report(x : string) is
    variable l : line;
  begin
    write(l, x);
    writeline(output, l);
  end procedure;
-- synthesis translate_on
begin
  o <= std_logic_vector(unsigned(a) + 2);
-- pragma translate_off
  sim : process (a) is
  begin
    sim_report("sim only");
-- synopsys translate_off
    -- Pragmas nest, so this inner region does not end the outer one
    sim_report("still sim only");
-- synopsys translate_on
  end process;
-- pragma translate_on
  o2 <= std_logic_vector(not unsigned(a));
end architecture rtl;
