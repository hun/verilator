-- DESCRIPTION: Verilator: VHDL record child for the element test
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package recpku is
  type pair_u is record
    x : std_logic_vector(3 downto 0);
    y : std_logic_vector(3 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.recpku.all;

entity child_u is
  port (
    i : in pair_u;
    o : out pair_u
  );
end;

architecture rtl of child_u is
begin
  o.x <= i.x;
  o.y <= i.y;
end;
