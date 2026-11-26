-- DESCRIPTION: Verilator: VHDL part association of an inout port is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
entity sub is port (p : inout std_logic_vector(3 downto 0)); end entity sub;
architecture rtl of sub is begin end architecture rtl;
library ieee;
use ieee.std_logic_1164.all;
entity t is port (b : in std_logic); end entity t;
architecture rtl of t is
begin
  u : entity work.sub port map (p(0 to 0) => b);
end architecture rtl;
