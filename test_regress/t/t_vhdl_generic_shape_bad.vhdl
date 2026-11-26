-- DESCRIPTION: Verilator: VHDL vector generic actual and default lengths
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- IEEE 1076-2008 14.3.3: a vector generic value must have the
-- declared length, for a default and for an actual.

library ieee;
use ieee.std_logic_1164.all;

entity leaf_bad_default is
  generic (g : std_logic_vector(3 downto 0) := "11");
  port (o : out integer);
end;

architecture rtl of leaf_bad_default is
begin
  o <= 1;
end;

library ieee;
use ieee.std_logic_1164.all;

entity leaf_bad_actual is
  generic (g : std_logic_vector(3 downto 0) := "1111");
  port (o : out integer);
end;

architecture rtl of leaf_bad_actual is
begin
  o <= 1;
end;

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (o : out integer);
end;

architecture rtl of t is
begin
  u1 : entity work.leaf_bad_default port map (o => open);
  u2 : entity work.leaf_bad_actual generic map (g => "11") port map (o => open);
end;
