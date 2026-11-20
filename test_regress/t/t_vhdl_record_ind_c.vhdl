-- DESCRIPTION: Verilator: VHDL constrained record child for the element test
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package recpkc is
  type pair_c is record
    x : std_logic_vector(3 downto 0);
    y : std_logic_vector(3 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.recpkc.all;

entity child_c is
  port (
    j : in pair_c;
    o : out pair_c
  );
end;

architecture rtl of child_c is
begin
  o.x <= j.x;
  o.y <= j.y;
end;
