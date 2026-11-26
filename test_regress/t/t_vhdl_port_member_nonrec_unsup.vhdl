-- DESCRIPTION: Verilator: VHDL record-member association on a non-record port is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity sub is port (p : in bit_vector(1 downto 0)); end entity sub;
architecture rtl of sub is begin end architecture rtl;
entity t is port (b : in bit); end entity t;
architecture rtl of t is
begin
  u : entity work.sub port map (p.f => b);
end architecture rtl;
