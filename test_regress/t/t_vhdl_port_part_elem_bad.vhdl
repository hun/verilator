-- DESCRIPTION: Verilator: VHDL element of an array port associated with an actual of another length
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
package pk is
  type byte is array (0 to 1) of bit_vector(3 downto 0);
end package pk;

use work.pk.all;

entity sub is
  port (p : in byte; y : out bit_vector(3 downto 0));
end entity sub;

architecture rtl of sub is
begin
  y <= p(1);
end architecture rtl;

use work.pk.all;

entity t is
  port (b8 : in bit_vector(7 downto 0); c : in bit_vector(3 downto 0);
        y : out bit_vector(3 downto 0));
end entity t;

architecture rtl of t is
begin
  u : entity work.sub port map (p(0) => b8, p(1) => c, y => y);
end architecture rtl;
