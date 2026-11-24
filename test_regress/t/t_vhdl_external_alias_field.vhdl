-- DESCRIPTION: Verilator: a field of an indexed alias of an external name
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
library ieee;
use ieee.std_logic_1164.all;
package p is
  type rec_t is record
    d : std_logic_vector(7 downto 0);
    r : std_logic;
  end record;
  type vec_t is array (0 to 1) of rec_t;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.p.all;
entity sub is
  port (a : in std_logic; o : out std_logic);
end;
architecture r of sub is
  signal v : vec_t;
begin
  v(0).d <= x"5a"; v(0).r <= a;
  v(1).d <= x"a5"; v(1).r <= not a;
  o <= a;
end;

library ieee;
use ieee.std_logic_1164.all;
use work.p.all;
entity t_vhdl_external_alias_field is
  port (a : in std_logic; res : out std_logic_vector(7 downto 0); b : out std_logic);
end;
architecture r of t_vhdl_external_alias_field is
begin
  de : entity work.sub port map (a => a, o => b);
  process (all)
    alias x is << signal de.v : vec_t >>;
  begin
    if x(1).r = '1' then res <= x(0).d; else res <= x(1).d; end if;
  end process;
end;
