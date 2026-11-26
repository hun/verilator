-- DESCRIPTION: Verilator: VHDL array port associated with an actual of another array type
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
package pk is
  type byte is array (0 to 1) of bit_vector(3 downto 0);
  type word is array (0 to 1) of bit_vector(3 downto 0);
end package pk;

use work.pk.all;

entity sub is
  port (p : in byte);
end entity sub;

architecture rtl of sub is
begin
end architecture rtl;

use work.pk.all;

entity t is
  port (w : in word);
end entity t;

architecture rtl of t is
begin
  u : entity work.sub port map (p => w);
end architecture rtl;
