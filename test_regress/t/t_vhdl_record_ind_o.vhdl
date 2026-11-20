-- DESCRIPTION: Verilator: VHDL record child for the artificial-open test
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package recpko is
  type pair_o is record
    a : std_logic_vector(3 downto 0);
    b : std_logic_vector(3 downto 0);
  end record;
end package;

library ieee;
use ieee.std_logic_1164.all;
use work.recpko.all;

-- Port `a` shares its name with record member `i.a`; the unassociated port must
-- still be recognized as an open of `a`, not treated as already associated
entity child_o is
  port (
    i : in pair_o;
    a : out std_logic_vector(3 downto 0)
  );
end;

architecture rtl of child_o is
begin
  a <= i.b;
end;
